# Object layout

A script object is reached as value -> holder -> instance -> field. Before this work each arrow was a separate heap block: the
holder in the engine's holder pool (96 bytes plus its control block), the instance in its `allocate_shared` block (control
block, 168-byte instance, then a slot table of one-word cell pointers and the 16-byte cells). A four-int object took about
450 bytes resident against Lua's ~120, and a field read through an element of an object array was three dependent cache
misses on three unrelated lines. The target is one block per object, the holder beside the instance, and a field read that
touches the instance's line and its cell's line.

## The block

```
[ block head: parts count ]  [ control_block<object_holder> | object_holder ]  [ std control block | class_instance ]  [ slot table | cells ]
```

`class_definition::allocate_instance` makes the whole block. The `std::shared_ptr<class_instance>` part and the holder part are
two owners of one allocation: the block head counts the parts that still need its memory (atomic: a `shared_ptr` may die on any
thread), the instance's allocator returns its part to the head instead of freeing, the embedded holder's `dealloc_fn` does the
same, and the last part frees the block. Neither part's counting changes: the holder keeps its non-atomic strong/weak counts
and its `data` keeps owning the instance through the `shared_ptr`, so every `holder->data` consumer, every
`shared_ptr<class_instance>` the host holds and every `weak_from_this` behaves as before. A holder that dies while the instance
lives (a host `shared_ptr` still holds it) leaves its bytes dead in the block until the instance goes; an instance kept only
by its holder dies with it.

The embedded holder is claimed by the first `make_class_object` over the instance; a second wrap of the same instance takes a
pooled holder as before. An instance made outside `allocate_instance` (deep copy's raw path, host-made instances) keeps the
pooled holder.

## Fields

Every slot the class has when the instance is made has a cell in the block (the arena). The slot table stays implicit while
the instance's layout is the one it was made with: `field_slot_value(slot)` is `arena + slot`, an absent field the raw-invalid
cell it already was. The first operation that re-seats slots (a reload's migration, a re-class, `relayout_fields`, a slot past
the arena) materializes the table: one word per slot pointing at the same arena cells, then proceeds as before. Cells never move
in either mode, so every address a reference, a closure, a coroutine's environment or a cell-loop site took stays valid.

## The rules this keeps

- Field-cell address stability: a cell is created in the arena or on the heap and destroyed only when its slot is retired or the
  instance dies; materializing the table copies pointers, never cells.
- References into fields (Borrowed References): a field reference pins the owner through `owner_pin` (the holder) or
  `owner_shared`; the pinned holder's `data` pins the instance, so the cell lives as long as the reference.
- Reload and retyping: migration and declared-type retypes run on the materialized table exactly as before; the field-shape
  epoch still moves, so cached slots and proofs refill.
- Re-class (`extend`): `reclass` materializes, then moves pointers between the classes' slot layouts.
- Parallel regions: a worker makes instances with embedded holders like the main thread; the block head's count is atomic, the
  holder's counts stay owner-thread as every holder's do (workers already mint plain holders under the same rule).
- Host boundary: `std::shared_ptr<class_instance>`, `weak_ptr`, `get_cpp_object_as` and the binder see the same objects; only
  where the bytes live changes.

## Stages

1. The embedded holder (one block per object, the holder beside the instance).
2. The implicit slot table (no per-slot pointer read, eight bytes less per field).
3. Header trims: the holder's host-only members and the instance's rarely used members move to their extras.
