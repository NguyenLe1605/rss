/**
 * File: news-aggregator.cc
 * --------------------------------
 * Presents the implementation of the NewsAggregator class.
 */

#include "news-aggregator.h"
#include <algorithm>
#include <cmath>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <libxml/catalog.h>
#include <libxml/parser.h>
#include <mutex>
#include <thread>
#include <unordered_set>
// you will almost certainly need to add more system header includes

// I'm not giving away too much detail here by leaking the #includes below,
// which contribute to the official CS110 staff solution.
#include "html-document-exception.h"
#include "html-document.h"
#include "ostreamlock.h"
#include "rss-feed-exception.h"
#include "rss-feed-list-exception.h"
#include "rss-feed-list.h"
#include "rss-feed.h"
#include "string-utils.h"
#include "utils.h"
using namespace std;

/**
 * Factory Method: createNewsAggregator
 * ------------------------------------
 * Factory method that spends most of its energy parsing the argument vector
 * to decide what rss feed list to process and whether to print lots of
 * of logging information as it does so.
 */
static const string kDefaultRSSFeedListURL = "small-feed.xml";
NewsAggregator *NewsAggregator::createNewsAggregator(int argc, char *argv[]) {
  struct option options[] = {
      {"verbose", no_argument, NULL, 'v'},
      {"quiet", no_argument, NULL, 'q'},
      {"url", required_argument, NULL, 'u'},
      {NULL, 0, NULL, 0},
  };

  string rssFeedListURI = kDefaultRSSFeedListURL;
  bool verbose = false;
  while (true) {
    int ch = getopt_long(argc, argv, "vqu:", options, NULL);
    if (ch == -1)
      break;
    switch (ch) {
    case 'v':
      verbose = true;
      break;
    case 'q':
      verbose = false;
      break;
    case 'u':
      rssFeedListURI = optarg;
      break;
    default:
      NewsAggregatorLog::printUsage("Unrecognized flag.", argv[0]);
    }
  }

  argc -= optind;
  if (argc > 0)
    NewsAggregatorLog::printUsage("Too many arguments.", argv[0]);
  return new NewsAggregator(rssFeedListURI, verbose);
}

/**
 * Method: buildIndex
 * ------------------
 * Initalizex the XML parser, processes all feeds, and then
 * cleans up the parser.  The lion's share of the work is passed
 * on to processAllFeeds, which you will need to implement.
 */
void NewsAggregator::buildIndex() {
  if (built)
    return;
  built = true; // optimistically assume it'll all work out
  xmlInitParser();
  xmlInitializeCatalog();
  processAllFeeds();
  xmlCatalogCleanup();
  xmlCleanupParser();
}

/**
 * Method: queryIndex
 * ------------------
 * Interacts with the user via a custom command line, allowing
 * the user to surface all of the news articles that contains a particular
 * search term.
 */
void NewsAggregator::queryIndex() const {
  static const size_t kMaxMatchesToShow = 15;
  while (true) {
    cout << "Enter a search term [or just hit <enter> to quit]: ";
    string response;
    getline(cin, response);
    response = trim(response);
    if (response.empty())
      break;
    const vector<pair<Article, int>> &matches =
        index.getMatchingArticles(response);
    if (matches.empty()) {
      cout << "Ah, we didn't find the term \"" << response << "\". Try again."
           << endl;
    } else {
      cout << "That term appears in " << matches.size() << " article"
           << (matches.size() == 1 ? "" : "s") << ".  ";
      if (matches.size() > kMaxMatchesToShow)
        cout << "Here are the top " << kMaxMatchesToShow << " of them:" << endl;
      else if (matches.size() > 1)
        cout << "Here they are:" << endl;
      else
        cout << "Here it is:" << endl;
      size_t count = 0;
      for (const pair<Article, int> &match : matches) {
        if (count == kMaxMatchesToShow)
          break;
        count++;
        string title = match.first.title;
        if (shouldTruncate(title))
          title = truncate(title);
        string url = match.first.url;
        if (shouldTruncate(url))
          url = truncate(url);
        string times = match.second == 1 ? "time" : "times";
        cout << "  " << setw(2) << setfill(' ') << count << ".) "
             << "\"" << title << "\" [appears " << match.second << " " << times
             << "]." << endl;
        cout << "       \"" << url << "\"" << endl;
      }
    }
  }
}

/**
 * Private Constructor: NewsAggregator
 * -----------------------------------
 * Self-explanatory.  You may need to add a few lines of code to
 * initialize any additional fields you add to the private section
 * of the class definition.
 */
NewsAggregator::NewsAggregator(const string &rssFeedListURI, bool verbose)
    : log(verbose), rssFeedListURI(rssFeedListURI), built(false) {}

/**
 * Private Method: processAllFeeds
 * -------------------------------
 * Downloads and parses the encapsulated RSSFeedList, which itself
 * leads to RSSFeeds, which themsleves lead to HTMLDocuemnts, which
 * can be collectively parsed for their tokens to build a huge RSSIndex.
 *
 * The vast majority of your Assignment 5 work has you implement this
 * method using multithreading while respecting the imposed constraints
 * outlined in the spec.
 */

void NewsAggregator::processAllFeeds() {
  RSSFeedList rssFeedList(rssFeedListURI);
  try {
    rssFeedList.parse();
  } catch (RSSFeedListException ex) {
    log.noteFullRSSFeedListDownloadFailureAndExit(rssFeedListURI);
  }
  log.noteFullRSSFeedListDownloadEnd();
  processFeeds(rssFeedList.getFeeds());
}

void NewsAggregator::processFeeds(const map<string, string> &feeds) {
  vector<thread> threads;
  for (const auto &[url, title] : feeds) {
    feedSem.wait();
    threads.emplace_back(thread(
        [this](const string &url) {
          feedLock.lock();
          if (seenUrls.count(url) == 1) {
            feedLock.unlock();
            feedSem.signal();
            return;
          }
          seenUrls.insert(url);
          feedLock.unlock();
          RSSFeed rssFeed(url);
          try {
            rssFeed.parse();
          } catch (RSSFeedException ex) {
            cerr << oslock << ex.what() << endl << osunlock;
            log.noteSingleFeedDownloadFailure(url);
            feedSem.signal();
            return;
          }
          processArticles(rssFeed.getArticles());
          feedSem.signal();
        },
        ref(url)));
  }
  for (auto &t : threads) {
    t.join();
  }
  // add to the index
  for (const auto &[host, titleMap] : serverTitleTokenMap) {
    for (const auto &[title, item] : titleMap) {
      index.add(item.first, item.second);
    }
  }
}

void NewsAggregator::processArticles(const vector<Article> &articles) {
  vector<thread> threads;
  for (const auto &article : articles) {
    articleSem.wait();
    threads.emplace_back(
        [this](const Article &article) {
          auto host = getURLServer(article.url);

          articleLock.lock();
          if (seenArticles.count(article) == 1) {
            articleLock.unlock();
            articleSem.signal();
            return;
          }
          seenArticles.insert(article);
          articleLock.unlock();

          semLock.lock();
          unique_ptr<Semaphore> &sem = serverSem[host];
          if (sem == nullptr)
            sem.reset(new Semaphore{8});
          semLock.unlock();

          sem->wait();
          log.noteSingleArticleDownloadBeginning(article);
          auto htmlDoc = HTMLDocument(article.url);
          try {
            htmlDoc.parse();
          } catch (HTMLDocumentException ex) {
            cerr << oslock << ex.what() << endl << osunlock;
            log.noteSingleArticleDownloadFailure(article);
            sem->signal();
            articleSem.signal();
            return;
          }

          vector<string> tokens = htmlDoc.getTokens();
          sort(tokens.begin(), tokens.end());
          vector<string> newTokens;
          Article newArticle = article;

          serverLock.lock();
          bool isDupe = serverTitleTokenMap.count(host) == 1 &&
                        serverTitleTokenMap[host].count(article.title) == 1;
          if (isDupe) {
            const auto &[currArticle, currToken] =
                serverTitleTokenMap[host][article.title];
            set_intersection(currToken.cbegin(), currToken.cend(),
                             tokens.cbegin(), tokens.cend(),
                             back_inserter(newTokens));
            newArticle = min(newArticle, currArticle);
          } else {
            newTokens = std::move(tokens);
          }
          serverTitleTokenMap[host][article.title] = {newArticle, newTokens};

          serverLock.unlock();
          sem->signal();
          articleSem.signal();
        },
        ref(article));
  }
  for (auto &t : threads) {
    t.join();
  }
}
