#include "abelcore/backend_interface.h"
#include "abelcore/backend_binder.h"
#include "abelcore/backend_handle_store.h"
#include "abelcore/backend_registry.h"
#include "abelcore/interpreter.h"
#include "abelcore/lexer.h"
#include "abelcore/parser.h"
#include "abelcore/resource_node.h"

#include <QtTest/QtTest>

#include <memory>

class AbelBackendResourceTests final : public QObject {
    Q_OBJECT

    abel::ResourceNode mathResourceNode() const
    {
        abel::ResourceNode node;
        node.id = QStringLiteral("math.backend");
        node.kind = QStringLiteral("qt_plugin");
        node.path = QStringLiteral("plugins/libmath_backend.so");
        node.iid = QStringLiteral(IAbelBackend_iid);
        node.backendId = QStringLiteral("MathSystem");
        node.qtVersion = abel::currentAbelQtVersion();
        node.kit = abel::currentAbelQtKit();
        node.platform = abel::currentAbelPlatform();
        node.compiler = abel::currentAbelCompiler();
        node.compilerVersion = abel::currentAbelCompilerVersion();
        node.cxxStandard = abel::currentAbelCxxStandard();
        node.abelAbi = abel::currentAbelAbi();
        node.symbols = {
            QStringLiteral("MathSystem.fast_add"),
            QStringLiteral("MathSystem.sort"),
            QStringLiteral("MathSystem.first_char"),
            QStringLiteral("MathSystem.char_code"),
            QStringLiteral("MathSystem.make_range"),
            QStringLiteral("MathSystem.sum_f64"),
            QStringLiteral("MathSystem.flip_bools"),
            QStringLiteral("MathSystem.scalar_refs"),
            QStringLiteral("MathSystem.fail_if_negative"),
            QStringLiteral("MathSystem.join_debug"),
            QStringLiteral("MathSystem.count_variadic"),
        };
        return node;
    }

private slots:
    void registryStoresFunctionDescriptors()
    {
        abel::BackendRegistry registry;
        abel::BackendFunctionDesc desc{
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            abel::makeType(abel::TypeKind::I32),
            {abel::makeType(abel::TypeKind::I32), abel::makeType(abel::TypeKind::I32)},
            false,
        };
        QVERIFY(registry.registerFunction(desc));
        QVERIFY(registry.hasBackend(QStringLiteral("MathSystem")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("fast_add")));

        const auto* found = registry.findFunction(QStringLiteral("MathSystem"), QStringLiteral("fast_add"));
        QVERIFY(found != nullptr);
        QCOMPARE(found->returnType.kind, abel::TypeKind::I32);
        QCOMPARE(found->params.size(), static_cast<size_t>(2));
    }

    void registryRejectsForeignFullSymbol()
    {
        abel::BackendRegistry registry;
        abel::Diagnostic diagnostic;
        QVERIFY(!registry.registerFunction({
            QStringLiteral("MathSystem"),
            QStringLiteral("Other.fast_add"),
            abel::makeType(abel::TypeKind::I32),
            {},
            false,
        }, &diagnostic));
        QCOMPARE(diagnostic.code, QStringLiteral("E0610"));
        QVERIFY(!registry.hasBackend(QStringLiteral("MathSystem")));
    }

    void registryReportsUnboundAndMissingSymbols()
    {
        abel::BackendRegistry registry;
        QList<abel::Diagnostic> diagnostics;

        registry.call({QStringLiteral("Missing"), QStringLiteral("fn"), {}, {}}, diagnostics);
        QCOMPARE(diagnostics.size(), 1);
        QCOMPARE(diagnostics.front().code, QStringLiteral("E0607"));

        diagnostics.clear();
        QVERIFY(registry.registerFunction({
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            abel::makeType(abel::TypeKind::I32),
            {},
            false,
        }));
        registry.call({QStringLiteral("MathSystem"), QStringLiteral("missing"), {}, {}}, diagnostics);
        QCOMPARE(diagnostics.size(), 1);
        QCOMPARE(diagnostics.front().code, QStringLiteral("E0608"));

        diagnostics.clear();
        registry.call({QStringLiteral("MathSystem"), QStringLiteral("fast_add"), {}, {}}, diagnostics);
        QCOMPARE(diagnostics.size(), 1);
        QCOMPARE(diagnostics.front().code, QStringLiteral("E0607"));
    }

    void registryCanBindRuntime()
    {
        abel::BackendRegistry registry;
        QVERIFY(registry.registerFunction({
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            abel::makeType(abel::TypeKind::I32),
            {},
            false,
        }));
        bool called = false;
        QString seenBackend;
        QString seenSymbol;
        size_t seenArgCount = 0;
        QVERIFY(registry.bindFunction(QStringLiteral("MathSystem"),
                                      QStringLiteral("fast_add"),
                                      [&called, &seenBackend, &seenSymbol, &seenArgCount](const abel::BackendCall& call, abel::AbelRuntimeContext&) {
                                          called = true;
                                          seenBackend = call.backendId;
                                          seenSymbol = call.symbol;
                                          seenArgCount = call.args.size();
                                          return abel::AbelValue::makeInt(call.args[0].asInt() + call.args[1].asInt());
                                      }));

        QList<abel::Diagnostic> diagnostics;
        auto value = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            {abel::AbelValue::makeInt(4), abel::AbelValue::makeInt(5)},
            {},
        }, diagnostics);
        QVERIFY(diagnostics.isEmpty());
        QVERIFY(called);
        QCOMPARE(seenBackend, QStringLiteral("MathSystem"));
        QCOMPARE(seenSymbol, QStringLiteral("MathSystem.fast_add"));
        QCOMPARE(seenArgCount, static_cast<size_t>(2));
        QCOMPARE(value.asInt(), 9);
    }

    void backendBinderDescribesFixedWidthIntegers()
    {
        auto fn = [](qint8 a, qint16 b, quint8 c, quint16 d, quint32 e, quint64 f) -> quint64 {
            return static_cast<quint64>(a) + static_cast<quint64>(b) + c + d + e + f;
        };
        auto desc = abel::AbelBackendBinder::describe(QStringLiteral("MathSystem.fixed"), fn);
        QCOMPARE(desc.returnType.kind, abel::TypeKind::U64);
        QCOMPARE(desc.params.size(), static_cast<size_t>(6));
        QCOMPARE(desc.params[0].kind, abel::TypeKind::I8);
        QCOMPARE(desc.params[1].kind, abel::TypeKind::I16);
        QCOMPARE(desc.params[2].kind, abel::TypeKind::U8);
        QCOMPARE(desc.params[3].kind, abel::TypeKind::U16);
        QCOMPARE(desc.params[4].kind, abel::TypeKind::U32);
        QCOMPARE(desc.params[5].kind, abel::TypeKind::U64);

        abel::AbelRuntimeContext ctx;
        auto runtime = abel::AbelBackendBinder::bind(fn);
        QList<abel::AbelValue> args{
            abel::AbelValue::makeInt(1, abel::TypeKind::I8),
            abel::AbelValue::makeInt(2, abel::TypeKind::I16),
            abel::AbelValue::makeInt(3, abel::TypeKind::U8),
            abel::AbelValue::makeInt(4, abel::TypeKind::U16),
            abel::AbelValue::makeInt(5, abel::TypeKind::U32),
            abel::AbelValue::makeInt(6, abel::TypeKind::U64),
        };
        auto value = runtime(args, ctx);
        for (const auto& d : ctx.diagnostics())
            qWarning() << d.code << d.message;
        QVERIFY(!ctx.hasError());
        QCOMPARE(value.type().kind, abel::TypeKind::U64);
        QCOMPARE(static_cast<quint64>(value.asInt()), quint64{21});
    }

    void backendBinderDescribesAndWritesScalarReferences()
    {
        auto fn = [](bool& flag, int& i, qint64& l, double& d, QString& s) {
            flag = !flag;
            i += 2;
            l += 3;
            d += 1.5;
            s += QStringLiteral(":out");
            return i;
        };
        auto desc = abel::AbelBackendBinder::describe(QStringLiteral("MathSystem.scalar_refs"), fn);
        QCOMPARE(desc.params.size(), static_cast<size_t>(5));
        for (const auto& param : desc.params)
            QVERIFY(param.isReference());
        QCOMPARE(desc.params[0].pointee->kind, abel::TypeKind::Bool);
        QCOMPARE(desc.params[1].pointee->kind, abel::TypeKind::I32);
        QCOMPARE(desc.params[2].pointee->kind, abel::TypeKind::I64);
        QCOMPARE(desc.params[3].pointee->kind, abel::TypeKind::F64);
        QCOMPARE(desc.params[4].pointee->kind, abel::TypeKind::Str);

        abel::AbelRuntimeContext ctx;
        auto runtime = abel::AbelBackendBinder::bind(fn);
        QList<abel::AbelValue> args{
            abel::AbelValue::makeBool(false),
            abel::AbelValue::makeInt(3),
            abel::AbelValue::makeInt(4, abel::TypeKind::I64),
            abel::AbelValue::makeDouble(2.5),
            abel::AbelValue::makeString(QStringLiteral("x")),
        };
        auto value = runtime(args, ctx);
        for (const auto& d : ctx.diagnostics())
            qWarning() << d.code << d.message;
        QVERIFY(!ctx.hasError());
        QCOMPARE(value.asInt(), 5);
        QCOMPARE(args[0].asBool(), true);
        QCOMPARE(args[1].asInt(), 5);
        QCOMPARE(args[2].asInt(), 7);
        QCOMPARE(args[3].asDouble(), 4.0);
        QCOMPARE(args[4].asString(), QStringLiteral("x:out"));
    }

    void backendHelpersProvideValueKeysAndHandles()
    {
        const auto one = abel::AbelValue::makeAny(abel::AbelValue::makeInt(1));
        const auto alsoOne = abel::AbelValue::makeInt(1);
        auto key = abel::AbelValueKey::fromValue(one);
        auto sameKey = abel::AbelValueKey::fromValue(alsoOne);
        QVERIFY(key.has_value());
        QVERIFY(sameKey.has_value());
        QVERIFY(*key == *sameKey);

        QHash<abel::AbelValueKey, abel::AbelValue> map;
        map.insert(*key, abel::AbelValue::makeString(QStringLiteral("one")));
        QCOMPARE(map.value(*sameKey).asString(), QStringLiteral("one"));

        const auto vectorKey = abel::AbelValue::makeVector(
            abel::makeType(abel::TypeKind::Any),
            {abel::AbelValue::makeAny(abel::AbelValue::makeString(QStringLiteral("x"))),
             abel::AbelValue::makeAny(abel::AbelValue::makeBool(true))});
        auto copiedVectorKey = abel::AbelValueKey::fromValue(vectorKey);
        QVERIFY(copiedVectorKey.has_value());
        map.insert(*copiedVectorKey, abel::AbelValue::makeInt(7));
        vectorKey.asVector()->elements[0] = abel::AbelValue::makeAny(abel::AbelValue::makeString(QStringLiteral("mutated")));
        auto mutatedVectorKey = abel::AbelValueKey::fromValue(vectorKey);
        QVERIFY(mutatedVectorKey.has_value());
        QVERIFY(*mutatedVectorKey != *copiedVectorKey);
        QVERIFY(!map.contains(*mutatedVectorKey));
        QCOMPARE(map.value(*copiedVectorKey).asInt(), 7);

        QString keyError;
        auto unsupported = abel::AbelValueKey::fromValue(abel::AbelValue::makeNullPointer(abel::makeType(abel::TypeKind::I32)),
                                                         &keyError);
        QVERIFY(!unsupported.has_value());
        QVERIFY(keyError.contains(QStringLiteral("unsupported AbelValue key type")));

        abel::AbelBackendHandleStore<QHash<abel::AbelValueKey, abel::AbelValue>> store;
        const qint64 handle = store.create();
        QVERIFY(store.contains(handle));
        abel::AbelRuntimeContext okCtx;
        QVERIFY(store.get(handle, okCtx) != nullptr);
        QVERIFY(!okCtx.hasError());

        abel::AbelRuntimeContext ctx;
        QVERIFY(store.get(999, ctx, QStringLiteral("test object")) == nullptr);
        QVERIFY(ctx.hasError());
        QCOMPARE(ctx.diagnostics().front().code, QStringLiteral("E0630"));
        QVERIFY(ctx.diagnostics().front().message.contains(QStringLiteral("test object")));
    }

    void backendBinderDescribesAndRejectsCallableArguments()
    {
        auto fn = [](abel::AbelCallable f, int x, abel::AbelRuntimeContext& ctx) {
            return f.call({abel::AbelValue::makeInt(x)}, ctx);
        };
        auto desc = abel::AbelBackendBinder::describe(QStringLiteral("Dyn.call_int"), fn);
        QCOMPARE(desc.returnType.kind, abel::TypeKind::Any);
        QCOMPARE(desc.params.size(), static_cast<size_t>(2));
        QCOMPARE(desc.params[0].kind, abel::TypeKind::Any);
        QCOMPARE(desc.params[1].kind, abel::TypeKind::I32);

        abel::AbelRuntimeContext ctx;
        auto runtime = abel::AbelBackendBinder::bind(fn);
        QList<abel::AbelValue> args{
            abel::AbelValue::makeInt(1),
            abel::AbelValue::makeInt(2),
        };
        auto value = runtime(args, ctx);
        QCOMPARE(value.type().kind, abel::TypeKind::Unknown);
        QVERIFY(ctx.hasError());
        QCOMPARE(ctx.diagnostics().front().code, QStringLiteral("E0621"));
        QVERIFY(ctx.diagnostics().front().message.contains(QStringLiteral("expects callable")));
    }

    void backendCanCallAbelFunctionValue()
    {
        abel::BackendRegistry registry;
        QVERIFY(registry.registerFunction({
            QStringLiteral("Dyn"),
            QStringLiteral("call_int"),
            abel::makeType(abel::TypeKind::Any),
            {abel::makeType(abel::TypeKind::Any), abel::makeType(abel::TypeKind::I32)},
            false,
        }));
        auto runtime = abel::AbelBackendBinder::bind([](abel::AbelCallable f, int x, abel::AbelRuntimeContext& ctx) {
            return f.call({abel::AbelValue::makeInt(x)}, ctx);
        });
        QVERIFY(registry.bindFunction(QStringLiteral("Dyn"),
                                      QStringLiteral("call_int"),
                                      [runtime](const abel::BackendCall& call, abel::AbelRuntimeContext& ctx) mutable {
                                          QList<abel::AbelValue> args;
                                          args.reserve(static_cast<qsizetype>(call.args.size()));
                                          for (const auto& arg : call.args)
                                              args.push_back(arg);
                                          return runtime(args, ctx);
                                      }));

        const QString src = QStringLiteral(R"(
            backend Dyn {
                fn any call_int(any f, int x);
            }

            fn int inc(int x) {
                return x + 1;
            }

            fn int main() {
                any f = inc;
                any y = Dyn::call_int(f, 41);
                int base = 5;
                func int(int) add_base = lambda [base] int(int x) {
                    return x + base;
                };
                any g = add_base;
                any z = Dyn::call_int(g, 10);
                return cast<int>(y) + cast<int>(z);
            }
        )");

        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<callable-backend-test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());

        abel::Interpreter interpreter;
        auto result = interpreter.run(*parsed.program, &registry);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 57);
    }

    void backendCallableDiagnosticsKeepStackFrames()
    {
        abel::BackendRegistry registry;
        QVERIFY(registry.registerFunction({
            QStringLiteral("Dyn"),
            QStringLiteral("call_str"),
            abel::makeType(abel::TypeKind::Any),
            {abel::makeType(abel::TypeKind::Any), abel::makeType(abel::TypeKind::Str)},
            false,
        }));
        auto runtime = abel::AbelBackendBinder::bind([](abel::AbelCallable f, QString s, abel::AbelRuntimeContext& ctx) {
            return f.call({abel::AbelValue::makeString(s)}, ctx);
        });
        QVERIFY(registry.bindFunction(QStringLiteral("Dyn"),
                                      QStringLiteral("call_str"),
                                      [runtime](const abel::BackendCall& call, abel::AbelRuntimeContext& ctx) mutable {
                                          QList<abel::AbelValue> args;
                                          args.reserve(static_cast<qsizetype>(call.args.size()));
                                          for (const auto& arg : call.args)
                                              args.push_back(arg);
                                          return runtime(args, ctx);
                                      }));

        const QString src = QStringLiteral(R"(
            backend Dyn {
                fn any call_str(any f, str s);
            }

            fn int inc(int x) {
                return x + 1;
            }

            fn int main() {
                any f = inc;
                any y = Dyn::call_str(f, "bad");
                return cast<int>(y);
            }
        )");

        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<callable-diagnostic-test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        QVERIFY(parsed.diagnostics.isEmpty());

        abel::Interpreter interpreter;
        auto result = interpreter.run(*parsed.program, &registry);
        QVERIFY(!result.diagnostics.isEmpty());
        const auto& diagnostic = result.diagnostics.front();
        bool sawBackend = false;
        bool sawFunction = false;
        for (const auto& frame : diagnostic.stackTrace) {
            sawBackend = sawBackend || frame.symbol == QStringLiteral("backend Dyn::call_str");
            sawFunction = sawFunction || frame.symbol == QStringLiteral("fn inc");
        }
        QVERIFY(sawBackend);
        QVERIFY(sawFunction);
    }

    void backendDynamicObjectParticipatesInIndexAndEquality()
    {
        abel::BackendRegistry registry;
        QVERIFY(registry.registerFunction({
            QStringLiteral("Dyn"),
            QStringLiteral("make_map"),
            abel::makeType(abel::TypeKind::Any),
            {},
            false,
        }));
        QVERIFY(registry.bindFunction(QStringLiteral("Dyn"),
                                      QStringLiteral("make_map"),
                                      [](const abel::BackendCall&, abel::AbelRuntimeContext&) {
                                          using Store = QHash<QString, abel::AbelValue>;
                                          auto store = std::make_shared<Store>();
                                          auto object = std::make_shared<abel::AbelDynamicObject>();
                                          object->kind = QStringLiteral("sdk.map");
                                          object->get = [store](const abel::AbelValue& key,
                                                                abel::AbelRuntimeContext& ctx,
                                                                const abel::SourceSpan& span) {
                                              if (key.type().kind != abel::TypeKind::Str) {
                                                  ctx.error(QStringLiteral("E0640"),
                                                            QStringLiteral("sdk.map key must be str, got %1")
                                                                .arg(key.type().displayName()),
                                                            span);
                                                  return abel::AbelValue::makeUnknown();
                                              }
                                              auto found = store->constFind(key.asString());
                                              if (found == store->constEnd()) {
                                                  ctx.error(QStringLiteral("E0640"),
                                                            QStringLiteral("sdk.map missing key '%1'").arg(key.asString()),
                                                            span);
                                                  return abel::AbelValue::makeUnknown();
                                              }
                                              return found.value();
                                          };
                                          object->set = [store](const abel::AbelValue& key,
                                                                const abel::AbelValue& value,
                                                                abel::AbelRuntimeContext& ctx,
                                                                const abel::SourceSpan& span) {
                                              if (key.type().kind != abel::TypeKind::Str) {
                                                  ctx.error(QStringLiteral("E0641"),
                                                            QStringLiteral("sdk.map key must be str, got %1")
                                                                .arg(key.type().displayName()),
                                                            span);
                                                  return;
                                              }
                                              store->insert(key.asString(), abel::unboxAny(value));
                                          };
                                          object->equals = [object](const abel::AbelValue& other,
                                                                    abel::AbelRuntimeContext&,
                                                                    const abel::SourceSpan&) -> std::optional<bool> {
                                              return other.isDynamicObject() && other.asDynamicObject() == object;
                                          };
                                          object->debug = [] {
                                              return QStringLiteral("<sdk.map>");
                                          };
                                          return abel::AbelValue::makeDynamicObject(object);
                                      }));

        const QString src = QStringLiteral(R"(
            backend Dyn {
                fn any make_map();
            }

            fn int main() {
                any obj = Dyn::make_map();
                obj["answer"] = 5;
                obj["text"] = "ok";
                any same = obj;
                bool eq = obj == same;
                bool neq = obj != "not object";
                if (eq && neq && any_is(obj, "dynamic:sdk.map") && any_debug(obj) == "<sdk.map>") {
                    str text = cast<str>(obj["text"]);
                    return cast<int>(obj["answer"]) + text.len();
                }
                return 0;
            }
        )");

        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<dynamic-object-test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());

        abel::Interpreter interpreter;
        auto result = interpreter.run(*parsed.program, &registry);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 7);
    }

    void backendDynamicObjectIndexDiagnosticsAreStable()
    {
        abel::BackendRegistry registry;
        QVERIFY(registry.registerFunction({
            QStringLiteral("Dyn"),
            QStringLiteral("make_readonly"),
            abel::makeType(abel::TypeKind::Any),
            {},
            false,
        }));
        QVERIFY(registry.bindFunction(QStringLiteral("Dyn"),
                                      QStringLiteral("make_readonly"),
                                      [](const abel::BackendCall&, abel::AbelRuntimeContext&) {
                                          auto object = std::make_shared<abel::AbelDynamicObject>();
                                          object->kind = QStringLiteral("readonly");
                                          object->get = [](const abel::AbelValue&,
                                                           abel::AbelRuntimeContext& ctx,
                                                           const abel::SourceSpan& span) {
                                              ctx.error(QStringLiteral("E0640"), QStringLiteral("readonly missing key"), span);
                                              return abel::AbelValue::makeUnknown();
                                          };
                                          return abel::AbelValue::makeDynamicObject(object);
                                      }));

        const QString readSrc = QStringLiteral(R"(
            backend Dyn {
                fn any make_readonly();
            }

            fn int main() {
                any obj = Dyn::make_readonly();
                any x = obj["missing"];
                return cast<int>(x);
            }
        )");
        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<dynamic-object-read-test>"), readSrc);
        QVERIFY(lexed.diagnostics.isEmpty());
        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        QVERIFY(parsed.diagnostics.isEmpty());
        abel::Interpreter interpreter;
        auto readResult = interpreter.run(*parsed.program, &registry);
        QVERIFY(!readResult.diagnostics.isEmpty());
        QCOMPARE(readResult.diagnostics.front().code, QStringLiteral("E0640"));
        QVERIFY(readResult.diagnostics.front().message.contains(QStringLiteral("missing key")));

        const QString writeSrc = QStringLiteral(R"(
            backend Dyn {
                fn any make_readonly();
            }

            fn int main() {
                any obj = Dyn::make_readonly();
                obj["x"] = 1;
                return 0;
            }
        )");
        auto writeLexed = lexer.lex(QStringLiteral("<dynamic-object-write-test>"), writeSrc);
        QVERIFY(writeLexed.diagnostics.isEmpty());
        auto writeParsed = parser.parse(writeLexed.tokens);
        QVERIFY(writeParsed.diagnostics.isEmpty());
        abel::Interpreter writeInterpreter;
        auto writeResult = writeInterpreter.run(*writeParsed.program, &registry);
        QVERIFY(!writeResult.diagnostics.isEmpty());
        QCOMPARE(writeResult.diagnostics.front().code, QStringLiteral("E0546"));
        QVERIFY(writeResult.diagnostics.front().message.contains(QStringLiteral("operator []=")));
    }

    void parsesValidResourceNodeJson()
    {
        const QString text = QStringLiteral(R"({
            "id": "math.backend",
            "kind": "qt_plugin",
            "path": "plugins/libmath_backend.so",
            "iid": "org.abel.IAbelBackend/1.0",
            "backendId": "MathSystem",
            "qtVersion": "%1",
            "kit": "%2",
            "platform": "%3",
            "compiler": "%4",
            "compilerVersion": "%5",
            "cxxStandard": "%6",
            "abelAbi": "%7",
            "symbols": [
                "MathSystem.fast_add",
                "MathSystem.sort",
                "MathSystem.first_char",
                "MathSystem.char_code",
                "MathSystem.make_range",
                "MathSystem.sum_f64",
                "MathSystem.flip_bools",
                "MathSystem.fail_if_negative",
                "MathSystem.join_debug",
                "MathSystem.count_variadic"
            ],
            "state": "unloaded",
            "lastError": ""
        })").arg(abel::currentAbelQtVersion(),
                 abel::currentAbelQtKit(),
                 abel::currentAbelPlatform(),
                 abel::currentAbelCompiler(),
                 abel::currentAbelCompilerVersion(),
                 abel::currentAbelCxxStandard(),
                 abel::currentAbelAbi());
        auto parsed = abel::resourceNodeFromJsonText(text, QStringLiteral("<test>"));
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.node.id, QStringLiteral("math.backend"));
        QCOMPARE(parsed.node.kind, QStringLiteral("qt_plugin"));
        QCOMPARE(parsed.node.backendId, QStringLiteral("MathSystem"));
        QCOMPARE(parsed.node.platform, abel::currentAbelPlatform());
        QCOMPARE(parsed.node.compiler, abel::currentAbelCompiler());
        QCOMPARE(parsed.node.compilerVersion, abel::currentAbelCompilerVersion());
        QCOMPARE(parsed.node.cxxStandard, abel::currentAbelCxxStandard());
        QCOMPARE(parsed.node.abelAbi, abel::currentAbelAbi());
        QCOMPARE(parsed.node.symbols.size(), 10);
        QCOMPARE(abel::resourceNodeStateName(parsed.node.state), QStringLiteral("unloaded"));
    }

    void rejectsInvalidResourceNodeJson()
    {
        const QString text = QStringLiteral(R"({
            "id": "broken",
            "kind": "file",
            "path": "plugins/libbad.so",
            "iid": "wrong",
            "backendId": "MathSystem",
            "qtVersion": "6.11.1",
            "kit": "gcc_64",
            "platform": "linux-x86_64",
            "compiler": "gcc",
            "compilerVersion": "14.2.0",
            "cxxStandard": "202302",
            "abelAbi": "abelcore-0",
            "symbols": []
        })");
        auto parsed = abel::resourceNodeFromJsonText(text, QStringLiteral("<test>"));
        QVERIFY(!parsed.diagnostics.isEmpty());
    }

    void rejectsResourceNodeSymbolOutsideBackend()
    {
        const QString text = QStringLiteral(R"({
            "id": "bad.symbol",
            "kind": "qt_plugin",
            "path": "plugins/libbad.so",
            "iid": "org.abel.IAbelBackend/1.0",
            "backendId": "MathSystem",
            "qtVersion": "6.11.1",
            "kit": "gcc_64",
            "platform": "linux-x86_64",
            "compiler": "gcc",
            "compilerVersion": "14.2.0",
            "cxxStandard": "202302",
            "abelAbi": "abelcore-0",
            "symbols": ["Other.fast_add"]
        })");
        auto parsed = abel::resourceNodeFromJsonText(text, QStringLiteral("<test>"));
        QVERIFY(!parsed.diagnostics.isEmpty());
    }

    void resourceJsonCheckDoesNotRejectForeignCompatibilityStrings()
    {
        const QString text = QStringLiteral(R"({
            "id": "foreign.kit",
            "kind": "qt_plugin",
            "path": "plugins/libmath_backend.so",
            "iid": "org.abel.IAbelBackend/1.0",
            "backendId": "MathSystem",
            "qtVersion": "0.0.0",
            "kit": "foreign_kit",
            "platform": "foreign-os-cpu",
            "compiler": "foreign-compiler",
            "compilerVersion": "0.0.0",
            "cxxStandard": "0",
            "abelAbi": "foreign-abi",
            "symbols": ["MathSystem.fast_add"]
        })");
        auto parsed = abel::resourceNodeFromJsonText(text, QStringLiteral("<test>"));
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.node.qtVersion, QStringLiteral("0.0.0"));
        QCOMPARE(parsed.node.kit, QStringLiteral("foreign_kit"));
        QCOMPARE(parsed.node.platform, QStringLiteral("foreign-os-cpu"));
        QCOMPARE(parsed.node.compiler, QStringLiteral("foreign-compiler"));
        QCOMPARE(parsed.node.compilerVersion, QStringLiteral("0.0.0"));
        QCOMPARE(parsed.node.cxxStandard, QStringLiteral("0"));
        QCOMPARE(parsed.node.abelAbi, QStringLiteral("foreign-abi"));
    }

    void resourceLoadRejectsQtVersionMismatch()
    {
        abel::ResourceNode node = mathResourceNode();
        node.qtVersion = QStringLiteral("0.0.0");

        abel::BackendRegistry registry;
        auto loaded = abel::loadBackendResourceNode(node, registry, QCoreApplication::applicationDirPath());
        QVERIFY(!loaded.ok());
        QCOMPARE(loaded.node.state, abel::ResourceNodeState::Failed);
        QCOMPARE(loaded.diagnostics.size(), 1);
        QCOMPARE(loaded.diagnostics.front().code, QStringLiteral("E0613"));
        QVERIFY(loaded.diagnostics.front().message.contains(QStringLiteral("Qt version")));
        QVERIFY(!registry.hasBackend(QStringLiteral("MathSystem")));
    }

    void resourceLoadRejectsQtKitMismatch()
    {
        abel::ResourceNode node = mathResourceNode();
        node.kit = QStringLiteral("foreign_kit");

        abel::BackendRegistry registry;
        auto loaded = abel::loadBackendResourceNode(node, registry, QCoreApplication::applicationDirPath());
        QVERIFY(!loaded.ok());
        QCOMPARE(loaded.node.state, abel::ResourceNodeState::Failed);
        QCOMPARE(loaded.diagnostics.size(), 1);
        QCOMPARE(loaded.diagnostics.front().code, QStringLiteral("E0613"));
        QVERIFY(loaded.diagnostics.front().message.contains(QStringLiteral("Qt kit")));
        QVERIFY(!registry.hasBackend(QStringLiteral("MathSystem")));
    }

    void resourceLoadRejectsPlatformCompilerAndAbiMismatch()
    {
        auto checkMismatch = [&](auto mutate, const QString& needle) {
            abel::ResourceNode node = mathResourceNode();
            mutate(node);
            abel::BackendRegistry registry;
            auto loaded = abel::loadBackendResourceNode(node, registry, QCoreApplication::applicationDirPath());
            QVERIFY(!loaded.ok());
            QCOMPARE(loaded.node.state, abel::ResourceNodeState::Failed);
            QCOMPARE(loaded.diagnostics.size(), 1);
            QCOMPARE(loaded.diagnostics.front().code, QStringLiteral("E0613"));
            QVERIFY2(loaded.diagnostics.front().message.contains(needle),
                     qPrintable(loaded.diagnostics.front().message));
            QVERIFY(!registry.hasBackend(QStringLiteral("MathSystem")));
        };

        checkMismatch([](abel::ResourceNode& node) {
            node.platform = QStringLiteral("foreign-os-cpu");
        }, QStringLiteral("platform"));
        checkMismatch([](abel::ResourceNode& node) {
            node.compiler = QStringLiteral("foreign-compiler");
        }, QStringLiteral("compiler"));
        checkMismatch([](abel::ResourceNode& node) {
            node.compilerVersion = QStringLiteral("0.0.0");
        }, QStringLiteral("compiler version"));
        checkMismatch([](abel::ResourceNode& node) {
            node.cxxStandard = QStringLiteral("0");
        }, QStringLiteral("C++ standard"));
        checkMismatch([](abel::ResourceNode& node) {
            node.abelAbi = QStringLiteral("foreign-abi");
        }, QStringLiteral("Abel ABI"));
    }

    void loadsMathBackendPluginAndCallsRegistry()
    {
        abel::ResourceNode node = mathResourceNode();

        abel::BackendRegistry registry;
        auto loaded = abel::loadBackendResourceNode(node, registry, QCoreApplication::applicationDirPath());
        for (const auto& d : loaded.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(loaded.ok());
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("fast_add")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("sort")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("first_char")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("char_code")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("make_range")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("sum_f64")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("flip_bools")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("scalar_refs")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("fail_if_negative")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("join_debug")));
        QVERIFY(registry.hasFunction(QStringLiteral("MathSystem"), QStringLiteral("count_variadic")));

        QList<abel::Diagnostic> diagnostics;
        auto sum = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("fast_add"),
            {abel::AbelValue::makeInt(1), abel::AbelValue::makeInt(2)},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(sum.asInt(), 3);

        diagnostics.clear();
        auto xs = abel::AbelValue::makeVector(abel::makeType(abel::TypeKind::I32),
                                              {abel::AbelValue::makeInt(3),
                                               abel::AbelValue::makeInt(1),
                                               abel::AbelValue::makeInt(2)});
        auto sorted = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("sort"),
            {xs},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(sorted.type().kind, abel::TypeKind::Void);
        QCOMPARE(xs.asVector()->elements[0].asInt(), 1);
        QCOMPARE(xs.asVector()->elements[1].asInt(), 2);
        QCOMPARE(xs.asVector()->elements[2].asInt(), 3);

        diagnostics.clear();
        auto firstChar = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("first_char"),
            {abel::AbelValue::makeString(QStringLiteral("Abel"))},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(firstChar.asChar(), QChar('A'));

        diagnostics.clear();
        auto charCode = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("char_code"),
            {abel::AbelValue::makeChar(QChar('A'))},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(charCode.asInt(), 65);

        diagnostics.clear();
        auto range = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("make_range"),
            {abel::AbelValue::makeInt(4)},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(range.type().kind, abel::TypeKind::Vector);
        QCOMPARE(range.type().pointee->kind, abel::TypeKind::I32);
        QCOMPARE(range.asVector()->elements.size(), static_cast<size_t>(4));
        QCOMPARE(range.asVector()->elements[3].asInt(), 3);

        diagnostics.clear();
        auto f64s = abel::AbelValue::makeVector(abel::makeType(abel::TypeKind::F64),
                                                {abel::AbelValue::makeDouble(1.5),
                                                 abel::AbelValue::makeDouble(2.5)});
        auto f64Sum = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("sum_f64"),
            {f64s},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(f64Sum.asDouble(), 4.0);

        diagnostics.clear();
        auto flags = abel::AbelValue::makeVector(abel::makeType(abel::TypeKind::Bool),
                                                 {abel::AbelValue::makeBool(true),
                                                  abel::AbelValue::makeBool(false)});
        auto flipped = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("flip_bools"),
            {flags},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(flipped.type().kind, abel::TypeKind::Void);
        QCOMPARE(flags.asVector()->elements[0].asBool(), false);
        QCOMPARE(flags.asVector()->elements[1].asBool(), true);

        diagnostics.clear();
        auto rejected = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("fail_if_negative"),
            {abel::AbelValue::makeInt(-1)},
            {},
        }, diagnostics);
        QCOMPARE(rejected.type().kind, abel::TypeKind::Unknown);
        QCOMPARE(diagnostics.size(), 1);
        QCOMPARE(diagnostics.front().code, QStringLiteral("E0623"));

        diagnostics.clear();
        auto joined = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("join_debug"),
            {abel::AbelValue::makeAny(abel::AbelValue::makeString(QStringLiteral("x="))),
             abel::AbelValue::makeAny(abel::AbelValue::makeInt(42)),
             abel::AbelValue::makeAny(abel::AbelValue::makeBool(true))},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(joined.asString(), QStringLiteral("x=42true"));

        diagnostics.clear();
        auto counted = registry.call({
            QStringLiteral("MathSystem"),
            QStringLiteral("count_variadic"),
            {abel::AbelValue::makeAny(abel::AbelValue::makeInt(1)),
             abel::AbelValue::makeAny(abel::AbelValue::makeString(QStringLiteral("two")))},
            {},
        }, diagnostics);
        for (const auto& d : diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(diagnostics.isEmpty());
        QCOMPARE(counted.asInt(), 2);
    }

    void loadedMathBackendRunsThroughInterpreter()
    {
        abel::ResourceNode node = mathResourceNode();

        abel::BackendRegistry registry;
        auto loaded = abel::loadBackendResourceNode(node, registry, QCoreApplication::applicationDirPath());
        for (const auto& d : loaded.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(loaded.ok());

        const QString src = QStringLiteral(R"(
            backend MathSystem {
                fn int fast_add(int a, int b = 5);
                fn void sort(vector<int>& xs);
                fn char first_char(str s);
                fn int char_code(char c);
                fn vector<int> make_range(int n);
                fn double sum_f64(vector<double> xs);
                fn void flip_bools(vector<bool>& xs);
                fn int scalar_refs(bool& flag, int& i, i64& l, f64& d, str& s);
                fn int fail_if_negative(int x);
                fn str join_debug(any... args);
                fn int count_variadic(any... args);
            }

            fn int main() {
                vector<int> xs = {3, 1, 2};
                vector<double> ds = {1.5, 2.5};
                vector<bool> flags = {true, false};
                MathSystem::sort(xs);
                MathSystem::flip_bools(flags);
                long big = 4;
                double scalar = 2.5;
                str text = "x";
                int scalar_refs = MathSystem::scalar_refs(flags[1], xs[0], big, scalar, text);

                int bonus = 0;
                if (flags[0]) {
                    bonus = 100;
                } else {
                    bonus = 10;
                }

                int n = MathSystem::make_range(4).len();
                int c = MathSystem::char_code(MathSystem::first_char("Abel"));
                int d = cast<int>(MathSystem::sum_f64(ds));
                int ok = MathSystem::fail_if_negative(5);
                str joined = MathSystem::join_debug("A", 7, true);
                int variadic_count = MathSystem::count_variadic("A", 7, true);
                vector<any> tail = {"A", 7, true};
                int named_fast = MathSystem::fast_add(b: 4, a: 5);
                int default_fast = MathSystem::fast_add(a: 5);
                int variadic_spread = MathSystem::count_variadic("prefix", ...tail);
                int pipe_fast = 3 |> MathSystem::fast_add(a: _, b: 4);
                int pipe_default_fast = 5 |> MathSystem::fast_add(a: _);
                int pipe_variadic_spread = tail |> MathSystem::count_variadic("prefix", ..._);
                any dyn_a = 6;
                int backend_dynamic = MathSystem::fast_add(dyn_a, 1);
                int joined_bonus = 0;
                if (joined == "A7true") {
                    joined_bonus = 6;
                } else {
                    joined_bonus = 100;
                }
                int refs_bonus = 0;
                if (!flags[1] && xs[0] == 3 && big == 7 && cast<int>(scalar) == 4 && text == "x:out") {
                    refs_bonus = scalar_refs;
                } else {
                    refs_bonus = 1000;
                }
                return MathSystem::fast_add(xs[0], xs[2]) + n + c + d + ok + bonus + joined_bonus + variadic_count + refs_bonus
                    + named_fast + default_fast + variadic_spread + pipe_fast + pipe_default_fast + pipe_variadic_spread + backend_dynamic;
            }
        )");

        abel::Lexer lexer;
        auto lexed = lexer.lex(QStringLiteral("<backend-test>"), src);
        QVERIFY(lexed.diagnostics.isEmpty());

        abel::Parser parser;
        auto parsed = parser.parse(lexed.tokens);
        for (const auto& d : parsed.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(parsed.diagnostics.isEmpty());

        abel::Interpreter interpreter;
        auto result = interpreter.run(*parsed.program, &registry);
        for (const auto& d : result.diagnostics)
            qWarning() << d.code << d.message;
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.exitCode, 173);
    }
};

QTEST_MAIN(AbelBackendResourceTests)

#include "test_backend_resource.moc"
