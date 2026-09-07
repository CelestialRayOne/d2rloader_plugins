# Engine Stability

Crash guards for Diablo II: Resurrected. No gameplay changes, no balance
changes, no presentation changes. Every effect of this plugin is either nothing
at all or the difference between a hard crash and a normal frame.

If you install one plugin from this repository, install this one.

## Supported builds

Diablo II: Resurrected **3.3.93847**, **3.2.92777**, Steam **3.3.93787**.

The plugin verifies the bytes at every address it touches before it touches
them. On a build it does not recognise it installs nothing, logs an error, and
stays loaded so `engine-stability` in the console can tell you why. An
unrecognised build is a no-op, never a mis-patch.

## What it fixes

### Dead-unit lookup guard (client)

**Symptom.** A crash on or shortly after the death of a unit that spawned
something which outlives it. The reproducible case is a summon with a 100%
on-death proc: the summon dies, its missiles are still in flight, and the game
faults while resolving the owner of one of those missiles.

**Cause.** Freeing a unit stamps a tombstone on the block (unit id `-1`) and
returns it to the pool. Every later resolution of that owner arrives at the
client's shared unit-by-id lookup, `sub_14009F270` at RVA `0009F270`, carrying
the tombstone id. That id masks to bucket `0x7F`, and the walk follows whatever
that slot holds. The function validates the chain but never validates the id it
was handed.

**Fix.** An inline hook answers "not found" for id `-1` without walking the
chain. Returning null is the function's own not-found result: it already ends in
`xor eax,eax / retn` when the chain runs out, so every one of its several
hundred call sites already handles it. A live unit can never carry id `-1`, so
no working lookup changes behaviour.

The guard is in the callee rather than in any caller on purpose. Captured dumps
from the 2.4 era arrived through more than one caller, so caller-side guards
were incomplete by construction.

**Cost.** One compare on the hot path. The diagnostic counter is only touched on
the guarded path, which should be rare.

## What it deliberately does not fix

Two guards that were necessary on 2.4 were checked against 3.3.93847 and found
to be **obsolete**. They are not ported, because installing a hook on a hot
function to fix a bug that is not there is a cost with no benefit.

### Server unit lookup null bucket: fixed by Blizzard

On 2.4 the server unit-by-(type,id) lookup dereferenced its bucket pointer with
no null check, and the bucket resolver returns null for any unit type outside
0-4. A tombstoned owner carries type 6, so it faulted.

On 3.x the equivalent function `sub_14048FE80` resolves the bucket through
`sub_1404925B0` and then does `if (!bucket) return 0;` at RVA `0048FECD`. The
bucket resolver still returns null for types outside 0-4, and the null result is
now handled natively. Nothing to add.

### Attack rating read on a non-player: fixed by Blizzard

On 2.4 the player attack-rating getter opened with a hard gate: any unit whose
type was not 0 fell through to `exit(-1)`, killing the process. Any softcode
formula that read the attack-rating stat in the context of a monster, hireling
or summon hit it, and the shipped workaround was to NOP the branch so the
non-player path fell into the normal body.

On 3.x the function is `sub_1403483C0`, and it has been restructured. A
non-player now goes through an assertion helper and then computes the value
normally, returning `tohit + 5 * (dexterity - 7)` and skipping only the
class-table `ToHitFactor` lookup that has no meaning for a non-player. That is
exactly the behaviour the 2.4 workaround produced. There is no longer an
`exit(-1)` branch to remove.

Note this is a binary-level observation about a crash. The underlying data
defect that triggered it on 2.4 was a `stat(` where `skill(` was meant in a
skills file, and that is still worth fixing in data wherever it appears.

## Not yet ported

Three further 2.4 stability patches are not in this release because their 3.x
counterparts have not been located and verified to the same standard:

- Cube transmute packet overflow guard
- Cube throw packet overflow guard
- Legacy graphics toggle disable

They will be added once each target is confirmed byte for byte. Shipping an
address that has only been guessed at would defeat the point of the plugin.

## Console

```
engine-stability
```

Reports whether each guard is active, and how many tombstoned lookups have been
suppressed so far. If the plugin loaded but the guard did not install, this is
where it says so.

## Technical notes

- Role: `Client`. The one guard is client-side, so the plugin does not need the
  `Shared` or `Server` role and is therefore **not recorded with saved
  characters**.
- Requires the `NativeHooks` flag, because it installs an inline hook.
- The displaced bytes at `0009F270` are `movsxd rax, edx` and
  `mov rax, [rcx+rax*8]`. Both are position independent, and no branch inside
  the function targets any address inside the displaced region, so the
  trampoline is safe.
- The full 41-byte body of the hooked function is verified, not only the seven
  bytes being displaced, so the plugin also refuses on a build where the
  not-found tail the guard depends on has changed.
- The expected-byte arrays are cross-checked against each other at compile time.
