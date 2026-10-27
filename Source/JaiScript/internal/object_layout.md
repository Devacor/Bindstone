# Object layout

A script object is reached as value -> instance -> field. An object value's storage is a `strong_ptr<object_holder>`, and a
class instance is its own holder: `class_instance` derives `script_value::object_holder` at offset 0, so the value's handle is
the instance's own `strong_ptr` and a field read is one dependent load from the value to the instance's line, then the cell.
A bound C++ object a value holds without an instance (a raw C++ object, a coroutine handle, a `T&` binding) has a
`host_holder` from the engine's holder pool.

## The block

```
[ definition pin? ]  [ head: prefix ]  [ control block | class_instance (object_holder at offset 0) ]  [ cells ]
```

`class_instance::make` makes the whole block. The head records how far the block's memory starts before it (a destructor
class's instance pins its definition there); the strong family's last weak reference frees the block. Every handle of the
object, a script value's, a `strong_ptr<class_instance>` the runtime keeps and a host `shared_ptr` over it alike, is one count
on the one control block: two wraps of one C++ object are the same object, and `use_count()` counts every owner.

`object_holder` carries what every object value reads without knowing which kind it holds: the type's name and id, the engine,
and the kind. Its accessors switch on the kind; a host holder's C++ object, bound address and anchors sit in `host_holder`.
`object_holder::last_release` ends either kind: an instance dies through its destructor chain, a host holder goes back to its
pool.

## Fields

Every slot the class has when the instance is made has a cell in the block (the arena). The slot table stays implicit while
the instance's layout is the one it was made with: `field_slot_value(slot)` is `arena + slot`, an absent field the raw-invalid
cell it already was. The first operation that re-seats slots (a reload's migration, a re-class, `relayout_fields`, a slot past
the arena) materializes the table: one word per slot pointing at the same arena cells. Cells never move in either mode, so every
address a reference, a closure, a coroutine's environment or a cell-loop site took stays valid.

## The rules this keeps

- Field-cell address stability: a cell is created in the arena or on the heap and destroyed only when its slot is retired or the
  instance dies; materializing the table copies pointers, never cells.
- References into fields: a field reference pins its owner through `owner_pin` (the instance itself), so the cell lives as long
  as the reference.
- Reload and retyping: migration and declared-type retypes run on the materialized table; the field-shape epoch moves, so
  cached slots and proofs refill.
- Re-class (`extend`): `reclass` materializes, moves pointers between the classes' slot layouts, and sets the instance's type.
  A plain wrapper of an object that was extended afterwards reads the layer through the object's slot at each access.
- Parallel regions: counts are plain. A worker never takes a count on a shared instance; it reads one through a borrow
  (internal/parallel_design.md, shared objects).
- Host boundary: `std::shared_ptr<class_instance>`, `weak_ptr`, `get_cpp_object_as` and the binder see the same objects.
