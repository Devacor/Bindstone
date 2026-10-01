# Language contracts and their tests

Each row is a rule and the test that pins it on both backends. The details live in
`closures_and_captures.md`, `reference_model.md` and `static_checking.md`.

| Boundary | Contract | Test |
|---|---|---|
| Required return | A primitive, string or reference result cannot fall off the body or return bare: a load error under strict checking, from a load-time walk that adds nothing to a call or a return. An end reached only through an unproven catch is a warning; warn and off modes run the program as written. | `Static Type Checking.required_results_are_checked_before_execution` |
| Reference parameter and return | A reference binds compatible storage and never converts: the exact primitive or math kind, the class or a subclass, the same concrete container or handle type. An argument of unknown element type stays lenient. | `Static Type Checking.known_reference_contracts_match_parameters_returns_and_grafts` |
| Capture list | `[]` captures nothing; `[x]` captures only `x`; `[=]`, `[&]` and init-captures `[x = e]` spell the rest. A nested lambda's name must be admitted by every lambda between it and the declaring frame. `this` and members need `[this]`, `[=]` or `[&]`. | `Static Type Checking.lambda_capture_lists_are_exhaustive` |
| Typed container in a `var` | A typed array copied into a `var`, or sliced, keeps its element constraint; an `array<var>` copy takes any element. | `Static Type Checking.documented_container_and_instance_capture_contracts` |
| Instance capture | `[this]` captures the instance itself: its reads and writes reach the original object. | `Static Type Checking.documented_container_and_instance_capture_contracts` |
| Field reload | A kept field keeps its value, converted where the no-script assignment table allows, else reset to the new default; added fields take their default, removed fields go. | `Hot Reload Tests.migration_reports_aggregate_actual_field_operations` |

## Observing reloads from the host

```cpp
engine->hot_reload_reporting(true);
engine->clear_hot_reload_reports();
engine->execute(updated_source);
for (const auto& report : engine->hot_reload_reports()) {
    // report.class_name, field_name, old_type, new_type, outcome, count
}
```

A report row aggregates the automatic field operations of a reload by class, field, stored
type before and after, and outcome (`preserved`, `converted`, `reset`, `added`, `removed`);
`count` is field operations across instances. Rows accumulate until cleared, so clear before
an edit for that edit's summary. A reload that changes no fields migrates nothing and adds no
rows. Rows hold metadata only: no values, no object identities, no hook outcomes.

Reporting is off by default; off, a reload records nothing and ordinary execution never
consults it. The host reads the rows on the engine's thread after the reload; no callback
runs during migration.
