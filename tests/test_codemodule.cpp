// TraceLab code blocks (lab/code): a C file compiled in memory by libtcc --
// its TL_* declarations read back in source order, parameters set by name
// (kept across a recompile, clamped), the tl_* functions reading the block's
// context, printf into the block's log, compiler errors with their line, a
// fault in the code caught with its line instead of ending the process (and
// the process still handling its own faults afterwards), the templates
// library's origin line, "modified" and "back to the original".

#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <stdexcept>

#include "lab/code/codefiles.h"
#include "lab/code/codemodule.h"
#include "lab/code/coderuntime.h"

using namespace tracelab::code;

namespace {

const char* const kController = R"(#include <tracelab.h>

TL_PARAM(kp, 2.0, "%/m", 0, 10);
TL_PARAM_INT(steps, 3, 1, 9);
TL_PARAM_ENUM(mode, 1, "frozen|taylor|iterative");
TL_IN(w_l);
TL_IN_ARRAY(s, 4);
TL_OUT(u_l);
TL_OUT_PLAN(u_r, 5);
TL_PREVIEW(px, py, 8);
TL_DIAG(err, "m");

static double last;

void ctl_reset(void) { last = 0; }

void ctl_step(void) {
    double big[3000];            /* > 4 KiB of stack: __chkstk on Windows */
    memset(big, 0, sizeof big);
    big[2999] = s[3];
    err = w_l - last;
    last = w_l;
    u_l = kp * err + big[2999] + steps + mode;
    for (int i = 0; i < 5; i++) u_r[i] = i * tl_ts();
    printf("tick %lld, %d points\n", tl_tick(), tl_preview_count());
    unsigned long long big64 = (unsigned long long)(1e19); /* __fixunsdfdi */
    if (big64 == 0) u_l = -1;
}
)";

}  // namespace

class TestCodeModule : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void declarationsInSourceOrder();
    void runsWithContext();
    void paramsKeptAcrossRecompile();
    void compileErrorHasLine();
    void faultIsCaughtWithLine();
    void hostFaultsStillReachTheHost();
    void plantDeclarations();
    void library();
};

void TestCodeModule::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QString error;
    QVERIFY2(CodeRuntime::instance().ensureReady(&error), qPrintable(error));
}

void TestCodeModule::declarationsInSourceOrder() {
    CodeModule module;
    QVERIFY2(module.compile(QString::fromLatin1(kController), QStringLiteral("pd.c")),
             qPrintable(module.errorText()));
    QStringList names;
    for (const Declaration& d : module.declarations()) {
        names.append(d.name);
    }
    QCOMPARE(names, (QStringList{"kp", "steps", "mode", "w_l", "s", "u_l", "u_r", "px", "err"}));
    const Declaration* s = module.find(DeclKind::In, QStringLiteral("s"));
    QVERIFY(s);
    QCOMPARE(s->count, 4);
    const Declaration* plan = module.find(DeclKind::Out, QStringLiteral("u_r"));
    QVERIFY(plan);
    QCOMPARE(plan->count, 5);
    const Declaration* kp = module.find(DeclKind::Param, QStringLiteral("kp"));
    QVERIFY(kp);
    QCOMPARE(kp->unit, QStringLiteral("%/m"));
    QCOMPARE(kp->defaultValue, 2.0);
    QCOMPARE(kp->maximum, 10.0);
    const Declaration* mode = module.find(DeclKind::ParamEnum, QStringLiteral("mode"));
    QVERIFY(mode);
    QCOMPARE(mode->options, (QStringList{"frozen", "taylor", "iterative"}));
    QCOMPARE(mode->maximum, 2.0);
    QVERIFY(module.hasFunction("ctl_step"));
    QVERIFY(module.hasFunction("ctl_reset"));
    QVERIFY(!module.hasFunction("filt_step"));
}

void TestCodeModule::runsWithContext() {
    CodeModule module;
    QVERIFY(module.compile(QString::fromLatin1(kController), QStringLiteral("pd.c")));
    module.context().ts = 0.0125;
    module.context().tick = 7;
    module.context().previewCount = 3;
    *module.find(DeclKind::In, QStringLiteral("w_l"))->values() = 1.5;
    module.find(DeclKind::In, QStringLiteral("s"))->values()[3] = 0.25;
    QVERIFY(module.call("ctl_reset"));
    QVERIFY(module.call("ctl_step"));
    // 2 * 1.5 + 0.25 + 3 + 1
    QCOMPARE(*module.find(DeclKind::Out, QStringLiteral("u_l"))->values(), 7.25);
    QCOMPARE(module.find(DeclKind::Out, QStringLiteral("u_r"))->values()[4], 4 * 0.0125);
    QCOMPARE(*module.find(DeclKind::Diag, QStringLiteral("err"))->values(), 1.5);
    QCOMPARE(module.takeLog(), QStringList{QStringLiteral("tick 7, 3 points")});
}

void TestCodeModule::paramsKeptAcrossRecompile() {
    CodeModule module;
    QVERIFY(module.compile(QString::fromLatin1(kController), QStringLiteral("pd.c")));
    QCOMPARE(module.paramValue(QStringLiteral("kp")), 2.0);
    QVERIFY(module.setParamValue(QStringLiteral("kp"), 4.5));
    QVERIFY(module.setParamValue(QStringLiteral("steps"), 42));  // clamped to 9
    QCOMPARE(module.paramValue(QStringLiteral("steps")), 9.0);
    QVERIFY(!module.setParamValue(QStringLiteral("nope"), 1));
    QString source = QString::fromLatin1(kController);
    source.replace(QStringLiteral("TL_PARAM(kp, 2.0, \"%/m\", 0, 10);"),
                   QStringLiteral("TL_PARAM(kp, 2.0, \"%/m\", 0, 3);"));
    QVERIFY(module.compile(source, QStringLiteral("pd.c")));
    QCOMPARE(module.paramValue(QStringLiteral("kp")), 3.0);  // the new range
    QCOMPARE(module.paramValue(QStringLiteral("steps")), 9.0);
    QCOMPARE(module.paramOverrides().value(QStringLiteral("kp")).toDouble(), 4.5);
}

void TestCodeModule::compileErrorHasLine() {
    CodeModule module;
    const QString source = QStringLiteral("#include <tracelab.h>\n\nvoid ctl_step(void) {\n    x = 1;\n}\n");
    QVERIFY(!module.compile(source, QStringLiteral("broken.c")));
    QVERIFY(!module.isCompiled());
    QVERIFY(!module.messages().isEmpty());
    const CodeMessage& first = module.messages().first();
    QCOMPARE(first.line, 4);
    QVERIFY(!first.warning);
    QVERIFY2(first.text.contains(QLatin1String("x")), qPrintable(first.text));
    QVERIFY(module.errorText().startsWith(QLatin1String("broken.c:4:")));

    // An unknown function is a link error, still reported.
    QVERIFY(!module.compile(QStringLiteral("void nope(void);\nvoid ctl_step(void) { nope(); }\n"),
                            QStringLiteral("link.c")));
    QVERIFY(!module.errorText().isEmpty());
}

void TestCodeModule::faultIsCaughtWithLine() {
    CodeModule module;
    const QString source = QStringLiteral(
        "#include <tracelab.h>\n"
        "TL_PARAM_BOOL(crash, 0);\n"
        "TL_OUT(u);\n"
        "void ctl_step(void) {\n"
        "    int *p = 0;\n"
        "    if (crash) u = *p;\n"
        "    else u = 1;\n"
        "}\n");
    QVERIFY(module.compile(source, QStringLiteral("crash.c")));
    QVERIFY(module.call("ctl_step"));
    QVERIFY(module.setParamValue(QStringLiteral("crash"), 1));
    QVERIFY(!module.call("ctl_step"));
    QVERIFY(module.hasRuntimeError());
    QCOMPARE(module.runtimeError().line, 6);
    QVERIFY2(module.runtimeError().text.contains(QLatin1String("memory")),
             qPrintable(module.runtimeError().text));
    // Stopped until cleared; then it runs again.
    QVERIFY(!module.call("ctl_step"));
    module.setParamValue(QStringLiteral("crash"), 0);
    module.clearRuntimeError();
    QVERIFY(module.call("ctl_step"));
    QCOMPARE(*module.find(DeclKind::Out, QStringLiteral("u"))->values(), 1.0);
}

void TestCodeModule::hostFaultsStillReachTheHost() {
    // With code compiled and relocated (tcc's fault handler installed), the
    // process's own C++ exceptions still go where they belong.
    CodeModule module;
    QVERIFY(module.compile(QString::fromLatin1(kController), QStringLiteral("pd.c")));
    bool caught = false;
    try {
        throw std::runtime_error("host");
    } catch (const std::runtime_error&) {
        caught = true;
    }
    QVERIFY(caught);
}

void TestCodeModule::plantDeclarations() {
    CodeModule module;
    const QString source = QStringLiteral(
        "#include <tracelab.h>\n"
        "TL_DISCRETE;\n"
        "TL_INPUT(u, \"%\", -100, 100);\n"
        "TL_STATE(w, \"rad/s\", 0.5);\n"
        "TL_OUTPUT_ARRAY(s, 3, \"\");\n"
        "void plant_update(void) { next(w) = 0.9 * w + 0.1 * u; }\n"
        "void plant_output(void) { for (int i = 0; i < 3; i++) s[i] = w * i; }\n");
    QVERIFY2(module.compile(source, QStringLiteral("plant.c")), qPrintable(module.errorText()));
    QVERIFY(module.hasDeclaration(DeclKind::Discrete));
    const Declaration* w = module.find(DeclKind::State, QStringLiteral("w"));
    QVERIFY(w && w->second);
    QCOMPARE(*w->values(), 0.5);
    const Declaration* u = module.find(DeclKind::Input, QStringLiteral("u"));
    QVERIFY(u);
    QCOMPARE(u->minimum, -100.0);
    *u->values() = 10.0;
    QVERIFY(module.call("plant_update"));
    QCOMPARE(*static_cast<double*>(w->second), 0.9 * 0.5 + 1.0);
    QVERIFY(module.call("plant_output"));
    QCOMPARE(module.find(DeclKind::Output, QStringLiteral("s"))->values()[2], 1.0);
}

void TestCodeModule::library() {
    CodeLibrary& library = CodeLibrary::instance();
    QTemporaryDir dir;
    const QString resourceDir = dir.path();
    CodeLibrary::writeFile(resourceDir + QStringLiteral("/t.c"), QStringLiteral("int a;\n"));
    library.registerTemplate({BlockKind::Filter, QStringLiteral("test/t"), QStringLiteral("T"),
                              QString(), resourceDir + QStringLiteral("/t.c")});
    QCOMPARE(library.templateSource(QStringLiteral("test/t")), QStringLiteral("int a;\n"));

    QString error;
    const QString path = library.createFile(BlockKind::Filter, QStringLiteral("test/t"),
                                            QString(), &error);
    QVERIFY2(!path.isEmpty(), qPrintable(error));
    QVERIFY(path.endsWith(QLatin1String("/filters/t.c")));
    const QString copy = CodeLibrary::readFile(path);
    QCOMPARE(CodeLibrary::originOf(copy), QStringLiteral("test/t"));
    QVERIFY(!library.isModified(copy));
    QVERIFY(!library.isModified(QString(copy).replace(QStringLiteral("\n"), QStringLiteral("\r\n"))));
    const QString edited = copy + QStringLiteral("int b;\n");
    QVERIFY(library.isModified(edited));
    QCOMPARE(library.originalOf(edited), copy);
    // A second copy gets a number.
    const QString second = library.createFile(BlockKind::Filter, QStringLiteral("test/t"),
                                              QString(), &error);
    QVERIFY(second.endsWith(QLatin1String("/filters/t_2.c")));
    // The skeletons compile.
    for (BlockKind kind : {BlockKind::Plant, BlockKind::Filter, BlockKind::Controller}) {
        CodeModule module;
        QVERIFY2(module.compile(CodeLibrary::skeletonSource(kind), QStringLiteral("skeleton.c")),
                 qPrintable(module.errorText()));
    }
    QFile::remove(path);
    QFile::remove(second);
}

QTEST_GUILESS_MAIN(TestCodeModule)
#include "test_codemodule.moc"
