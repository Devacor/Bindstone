// JaiScript registrars are now distributed to their respective .cpp files
// Each class has its registrar in the .cpp file where it's implemented
//
// Pattern:
// static jai::registrar<ClassName, MV::Services> _hookClassName("ScriptName",
//     [](jai::dynamic_binder<ClassName>& builder, const MV::Services&) {
//         builder.auto_bind();
//         // ... additional bindings
//     }
// );
//
// All registrars are triggered via: jai::bind_registrar<MV::Services>(engine, services);
