#include <algorithm>
#include <atomic>
#include <execution>
#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>

#include <fmt/format.h>
#include <fmt/os.h>
#include <httplib.h>
#include <lexbor/css/css.h>
#include <lexbor/html/html.h>
#include <lexbor/selectors/selectors.h>
#include <omp.h>

using namespace std::literals;

using DomainPath = std::pair<std::string, std::string>;
using Links = std::vector<std::string>;

DomainPath splitUrl(const std::string &url);
std::string sanitizePath(const std::string &path);

template <typename Func>
void processPage(const std::string &html, const std::string &cssSelector, Func f);

std::string getMangaNameFromMangaPage(const std::string &html);
Links getChapterLinksFromMangaPage(const std::string &html);
void saveChapterImages(const std::filesystem::path &mangaDirPath, const std::string &html);

constexpr const char *manganato = "https://readmanganato.com";

constexpr std::string_view mangaDir = "mangas";

std::mutex chaptersCompletedMutex;

int main(int argc, char *argv[]) {
    const char *mangaPath = argv[1];

    // Create manga dir
    const std::filesystem::path mangasDirPath = mangaDir;
    std::filesystem::create_directory(mangasDirPath);

    // Download the manga page
    fmt::print("Downloading from {}{}\n", manganato, mangaPath);

    httplib::Client cli(manganato);
    const auto res = cli.Get(mangaPath);
    fmt::print("{}{}: {}\n", manganato, mangaPath, res->status);

    // Get manga name and print directory of manga
    const auto mangaName = getMangaNameFromMangaPage(res->body);
    fmt::print("Manga: {}\n", mangaName);
    const auto sanitizedMangaName = sanitizePath(mangaName);
    const std::filesystem::path mangaDirPath = mangasDirPath / sanitizedMangaName;
    std::filesystem::create_directory(mangaDirPath);
    fmt::print("Manga Directory: {}\n", mangaDirPath.string());

    const std::filesystem::path chaptersCompletedPath = mangaDirPath / "chapters-completed.txt";
    // create the file if not exist
    std::unordered_set<std::string> chaptersCompleted;

    { std::ofstream chaptersCompletedFile(chaptersCompletedPath, std::ios::app); }
    {
        // read into the set
        std::ifstream chaptersCompletedFile(chaptersCompletedPath);
        std::string line;
        while (std::getline(chaptersCompletedFile, line)) {
            chaptersCompleted.insert(line);
        }
    }
    std::ofstream chaptersCompletedFile(chaptersCompletedPath, std::ios::app);

    std::atomic_size_t count = 0;
    Links remainingChapterLinks;
    const auto chapterLinks = getChapterLinksFromMangaPage(res->body);

    for (size_t i = chapterLinks.size(); i-- > 0;) {
        // check if chapter is completed
        const auto &chapterLink = chapterLinks[i];
        if (chaptersCompleted.find(chapterLink) != chaptersCompleted.end()) {
            fmt::print(
                    "{}/{}: (already completed) {}\n", ++count, chapterLinks.size(), chapterLink);
        } else {
            // filter now so that omp threads later will share workload evenly
            // otherwise some threads will have less or no workload to do
            remainingChapterLinks.push_back(std::move(chapterLink));
        }
    }

    std::reverse(remainingChapterLinks.begin(), remainingChapterLinks.end());

#pragma omp parallel for
    for (size_t i = remainingChapterLinks.size() - 1; i >= 0; --i) {
        const auto &chapterLink = remainingChapterLinks[i];
        const auto threadId = omp_get_thread_num();

        const auto [_, path] = splitUrl(chapterLink);

        const auto chapterDirPath =
                mangaDirPath /
                sanitizePath(std::string(path.cbegin() + path.rfind('/') + 1, path.cend()));
        std::filesystem::create_directory(chapterDirPath);

        // declare a new client for each thread, sharing client is not thread safe
        httplib::Client cli(manganato);
        const auto res = cli.Get(path.c_str());
        if (res->status != 200) {
            fmt::print("Failed to load {}{}\n", manganato, path);
            continue;
        }

        try {
            saveChapterImages(chapterDirPath, res->body);
        } catch (const std::runtime_error *err) {
            fmt::print("{}\n", err->what());
            continue;
        }

        // append completed chapter
        {
            const std::lock_guard<std::mutex> lock(chaptersCompletedMutex);
            chaptersCompletedFile << chapterLink << '\n';
            chaptersCompletedFile.flush();
        }

        fmt::print("{}/{}: {} {}\n", ++count, chapterLinks.size(), threadId, chapterLink);
    }

    return EXIT_SUCCESS;
}

DomainPath splitUrl(const std::string &url) {
    // https:// -> 8 chars, thus search from index 8
    // which also works for http:// as the domain has to be at least 1 char
    const size_t pathIndex = url.find('/', 8);

    return {std::string(url, 0, pathIndex), std::string(url, pathIndex)};
}

// Simple sanitization of file path
// ' ' -> -
// ~`!@#$%^&*()+=[]\{}|;':",./<>?\r\n\t -> _
std::string sanitizePath(const std::string &path) {
    const std::regex spaceRegex(" ");
    const std::regex invalidRegex(
            R"a([\~\`\!\@\#\$\%\^\&\*\(\)\+\=\[\]\\\{\}\|\;\'\:\"\,\.\/\<\>\?\r\n\t])a");

    auto sanitized = std::regex_replace(path, spaceRegex, "-");
    sanitized = std::regex_replace(sanitized, invalidRegex, "_");

    return sanitized;
}

template <typename Func>
void processPage(const std::string &html, const std::string &cssSelector, Func f) {
    /* Create HTML Document. */

    lxb_status_t status;

    lxb_html_document_t *document = lxb_html_document_create();
    status = lxb_html_document_parse(document, (const lxb_char_t *)html.c_str(), html.size());
    if (status != LXB_STATUS_OK) {
        throw new std::runtime_error("Failed to parse manga page html");
    }

    lxb_dom_node_t *body = lxb_dom_interface_node(lxb_html_document_body_element(document));

    /* CSS Selector. */

    const lxb_char_t *lxbCssSelector = (const lxb_char_t *)cssSelector.c_str();

    /* Create CSS parser. */

    lxb_css_parser_t *cssParser = lxb_css_parser_create();
    status = lxb_css_parser_init(cssParser, NULL, NULL);
    if (status != LXB_STATUS_OK) {
        throw new std::runtime_error("Failed to init css parser");
    }

    /* Create CSS Selector parser. */

    lxb_css_selectors_t *lxbCssSelectors = lxb_css_selectors_create();
    status = lxb_css_selectors_init(lxbCssSelectors, 32);
    if (status != LXB_STATUS_OK) {
        throw new std::runtime_error("Failed to init css selectors");
    }

    /* It is important that a new selector object is not created internally
     * for each call to the parser.
     */
    lxb_css_parser_selectors_set(cssParser, lxbCssSelectors);

    /* Selectors. */

    lxb_selectors_t *selectors = lxb_selectors_create();
    status = lxb_selectors_init(selectors);
    if (status != LXB_STATUS_OK) {
        throw new std::runtime_error("Failed to init selectors");
    }

    /* Parse and get the log. */

    lxb_css_selector_list_t *results =
            lxb_css_selectors_parse(cssParser, lxbCssSelector, cssSelector.size());
    if (results == NULL) {
        throw new std::runtime_error(fmt::format(
                "Failed to parse css selectors, {}, {}", cssParser->status, cssSelector));
    }

    /* Find HTML nodes by CSS Selectors. */
    status = lxb_selectors_find(
            selectors, body, results,
            [](auto *node, auto *spec, void *ctx) -> lxb_status_t {
                auto f = static_cast<Func *>(ctx);
                return (*f)(node, spec);
            },
            &f);
    if (status != LXB_STATUS_OK) {
        throw new std::runtime_error("Failed to extract chapter links from manga page");
    }

    /* Destroy Selectors object. */
    lxb_selectors_destroy(selectors, true);

    /* Destroy resources for CSS Parser. */
    lxb_css_parser_destroy(cssParser, true);

    /* Destroy CSS Selectors List memory. */
    // lxb_css_selectors_destroy(lxbCssSelectors, true, true);
    /* or use */
    lxb_css_selector_list_destroy_memory(results);

    /* Destroy HTML Document. */
    lxb_html_document_destroy(document);
}

std::string getMangaNameFromMangaPage(const std::string &html) {
    std::string mangaName;

    const auto cssSelector = "div.story-info-right h1"s;

    auto getMangaName = [&](auto *node, auto *spec) -> lxb_status_t {
        size_t valueSize;
        const auto *value = lxb_dom_node_text_content(node, &valueSize);
        mangaName = std::string((const char *)value, valueSize);

        return LXB_STATUS_OK;
    };

    processPage(html, cssSelector, getMangaName);

    return mangaName;
}

Links getChapterLinksFromMangaPage(const std::string &html) {
    Links chapterLinks;

    const auto cssSelector = "a.chapter-name"s;

    auto getChapterLinks = [&](auto *node, auto *spec) -> lxb_status_t {
        auto *element = lxb_dom_interface_element(node);
        static const lxb_char_t hrefAttr[] = "href";
        size_t valueSize;
        const auto *value =
                lxb_dom_element_get_attribute(element, hrefAttr, sizeof(hrefAttr) - 1, &valueSize);

        chapterLinks.emplace_back((const char *)value, valueSize);

        return LXB_STATUS_OK;
    };

    processPage(html, cssSelector, getChapterLinks);

    return chapterLinks;
}

void saveChapterImages(const std::filesystem::path &chapterDirPath, const std::string &html) {
    const auto cssSelector = "div.container-chapter-reader img"s;

    size_t part = 1;
    auto saveImage = [&](auto *node, auto *spec) -> lxb_status_t {
        auto *element = lxb_dom_interface_element(node);
        static const lxb_char_t hrefAttr[] = "src";
        size_t valueSize;
        const auto *value =
                lxb_dom_element_get_attribute(element, hrefAttr, sizeof(hrefAttr) - 1, &valueSize);
        const std::string imageLink((const char *)value, valueSize);

        const auto [domain, path] = splitUrl(imageLink);

        httplib::Client cli(domain.c_str());
        const httplib::Headers headers = {{"Referer", manganato}};
        cli.set_default_headers(headers);
        const auto res = cli.Get(path.c_str());

        if (res->status != 200) {
            return LXB_STATUS_ERROR;
        }

        const auto imageName =
                fmt::format("{}{}", std::to_string(part), std::string(path, path.rfind(".")));

        const auto imagePath = chapterDirPath / imageName;

        auto imageFile = fmt::output_file(imagePath.c_str());
        imageFile.print("{}", res->body);

        part++;
        return LXB_STATUS_OK;
    };

    processPage(html, cssSelector, saveImage);
}
