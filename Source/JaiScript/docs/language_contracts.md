# Language contracts under review

This page maps the revised rules to source regressions. These patches are drafts:
the added cases have not been compiled or run. The existing executable used for
the KITE demo predates these drafts and does not validate them.

| Boundary | Contract | Executable case |
|---|---|---|
| Required return | A named primitive/string/reference result cannot structurally fall off the body. Strict checking rejects it before execution. Uncertain exception paths remain advisory; warn/off retain unchecked behavior. There is no new per-call return guard. | `Static Type Checking.required_results_are_checked_before_execution` |
| Reference parameter/return | Bind compatible storage without conversion. Primitive/math kinds match exactly; classes admit subclasses; known concrete containers and handles are invariant. Unknown container views retain current leniency. | `Static Type Checking.known_reference_contracts_match_parameters_returns_and_grafts` |
| Capture list | `[]` captures nothing. `[=]`, `[&]`, named and initialized captures express intent; a named list does not acquire unlisted locals. Nested closures need each intervening lambda to admit the name. | `Static Type Checking.lambda_capture_lists_are_exhaustive` |
| Dynamic container holder | A typed container copied into `var`, or sliced, retains element constraints. An explicit `array<var>` copy permits dynamic elements. | `Static Type Checking.documented_container_and_instance_capture_contracts` |
| Instance capture | `[this]` pins the original object and accesses its fields. It does not copy the instance. | `Static Type Checking.documented_container_and_instance_capture_contracts` |
| Field reload | Existing values are preserved or converted where the no-script assignment table permits; otherwise they reset to the new default. Added/removed fields change the live schema. | `Hot Reload Tests.migration_reports_aggregate_actual_field_operations` |

`closures_and_captures.md`, `reference_model.md`, and `static_checking.md` give the
details. Historical design notes and older site descriptions need reconciliation
when a contract changes; they are not an alternative runtime specification.

## Observing reloads from the host

```cpp
engine->hot_reload_reporting(true);
engine->clear_hot_reload_reports();
engine->execute(updatedSource);
for (const auto& report : engine->hot_reload_reports()) {
	// class_name, field_name, old_type, new_type, outcome, count
	// Present this metadata in the editor's reload log.
}
```

Reports aggregate actual automatic field operations by class, field, stored type
names and outcome. `preserved`, `converted`, `reset`, `added`, and `removed` describe
the operation; counts are field operations across instances, not unique objects.
They do not include values, object identities, hook failures, or a transaction ID.
Clear between edits to obtain an edit-local summary. A fields-unchanged reload may
perform no migrations and yield no rows. Hook changes are observed only at the
subsequent automatic migration boundary; reports are not an old declared-schema diff.

Reporting is disabled by default. Disabled reporting allocates no report metadata;
ordinary function calls and field accesses do no report work. Hosts poll after reload
on the engine's owning thread. No reporting callback executes during migration, and
the report does not retain any instance.

## Development evidence

The KITE fresh-developer exercise ran a copied existing harness without compiling.
It demonstrated same-process edits, state preservation, rejection of a malformed
module, field addition, and a separate removal probe. Builtin API discovery and
migration visibility were the largest authoring frictions. Its older-timestamp copy
attempt also retained cache/source evidence for a separate audit; that observation
does not establish a defect in the current source revision.
