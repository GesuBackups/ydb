
#include <library/cpp/testing/unittest/registar.h>
#include <library/cpp/robots_txt/robots_txt.h>
#include <library/cpp/robots_txt/constants.h>

#include "robots_repacker.h"

class TRobotsRepackerTest: public TTestBase {
    UNIT_TEST_SUITE(TRobotsRepackerTest);
    UNIT_TEST(TestPackUnpackAnyBot)
    UNIT_TEST(TestPackUnpackYandexBot)
    UNIT_TEST_SUITE_END();
private:

    inline void TestPackUnpack(const ui32 botId, const TString& userAgent) {

        const TString& sss = "User-agent: " + userAgent + "\n" +
                        "Allow: /foo\n"
                        "Disallow: /\n"
                        "Host: www.somehost.ru\n"
                        "Clean-param: trololo\n"
                        "Clean-param: ololo\n"
                        "Sitemap: http://imdb.com/sitemap.xml\n";

        TStringInput input(sss);

        TRobotsTxtParser parser(input);
        TSet<ui32> botIds;
        botIds.insert(botId);
        TRobotsTxt robots(botIds, robots_max, -1, false);
        TRobotsTxt::ParseRules(parser, &robots, &robots);

        const char* newRobotsPacked = nullptr;
        ui32 newRobotsPackedSize = robots.GetPacked(newRobotsPacked); // get packed robots in new format

        char oldRobotsPacked[65536], newRobotsPacked2[65536];
        ui32 oldRobotsPackedSize, newRobotsPackedSize2;

        RepackRobotsNewToOld(
            botId,
            newRobotsPacked,
            newRobotsPackedSize,
            oldRobotsPacked,
            oldRobotsPackedSize);

        RepackRobotsOldToNew(
            oldRobotsPacked,
            oldRobotsPackedSize,
            newRobotsPacked2,
            newRobotsPackedSize2,
            botId);

        UNIT_ASSERT_EQUAL(newRobotsPackedSize, newRobotsPackedSize2);
        for (ui32 byte = 0; byte < newRobotsPackedSize; ++byte) {
            UNIT_ASSERT_EQUAL(newRobotsPacked[byte], newRobotsPacked2[byte]);
        }

    }

    inline void TestPackUnpackAnyBot() {
        TestPackUnpack(robotstxtcfg::id_anybot, "*");
    }

    inline void TestPackUnpackYandexBot() {
        TestPackUnpack(robotstxtcfg::id_yandexbot, "Yandex");
    }
};

UNIT_TEST_SUITE_REGISTRATION(TRobotsRepackerTest);
