#include "core/web_language.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace whatsie::core;

class TestWebLanguage : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void autoDetectIsFirstAndDefault()
    {
        const auto langs = availableWebLanguages();
        QVERIFY(!langs.isEmpty());
        QVERIFY(langs.first().code.isEmpty());
        QVERIFY(langs.first().displayName.contains(u"Auto-detect"_s));

        // When code is empty, no forced header or script override
        QVERIFY(acceptLanguageHeader(QString()).isEmpty());
        QVERIFY(acceptLanguageHeader(u"   "_s).isEmpty());
        QVERIFY(webClientLanguages(QString()).isEmpty());
        QVERIFY(webClientLanguages(u"   "_s).isEmpty());
    }

    void acceptHeadersCorrectlyFormatted()
    {
        // Polish
        QCOMPARE(acceptLanguageHeader(u"pl_PL"_s), u"pl-PL,pl;q=0.9,en-US;q=0.8,en;q=0.7"_s);

        // German
        QCOMPARE(acceptLanguageHeader(u"de_DE"_s), u"de-DE,de;q=0.9,en-US;q=0.8,en;q=0.7"_s);

        // English US / GB
        QCOMPARE(acceptLanguageHeader(u"en_US"_s), u"en-US,en;q=0.9"_s);
        QCOMPARE(acceptLanguageHeader(u"en_GB"_s), u"en-GB,en;q=0.9"_s);
    }

    void clientLanguagesContainExpectedList()
    {
        const QStringList pl = webClientLanguages(u"pl_PL"_s);
        QCOMPARE(pl.value(0), u"pl-PL"_s);
        QCOMPARE(pl.value(1), u"pl"_s);
        QVERIFY(pl.contains(u"en-US"_s));
        QVERIFY(pl.contains(u"en"_s));

        const QStringList en = webClientLanguages(u"en_US"_s);
        QCOMPARE(en.value(0), u"en-US"_s);
        QCOMPARE(en.value(1), u"en"_s);
        QCOMPARE(en.size(), 2);
    }

    void allLocalesHaveDisplayNameAndSearchKey()
    {
        const auto langs = availableWebLanguages();
        QVERIFY(langs.size() > 50);

        bool foundPolish = false;
        bool foundEnglish = false;
        for (const auto& entry : langs) {
            QVERIFY(!entry.displayName.isEmpty());
            QVERIFY(!entry.searchKey.isEmpty());
            if (entry.code == u"pl_PL"_s) {
                foundPolish = true;
                QVERIFY(entry.searchKey.contains(u"polish"_s));
                QVERIFY(entry.searchKey.contains(u"polski"_s));
            }
            if (entry.code == u"en_US"_s) {
                foundEnglish = true;
                QVERIFY(entry.searchKey.contains(u"english"_s));
            }
        }
        QVERIFY(foundPolish);
        QVERIFY(foundEnglish);
    }
};

QTEST_MAIN(TestWebLanguage)
#include "tst_web_language.moc"
