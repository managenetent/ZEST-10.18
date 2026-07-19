# z0.user - user account association (exploration only, nothing built)

Status: **VISION / NOT DESIGNED IN DETAIL, NOT IMPLEMENTED**, same
framing as mutaclsym's own `platform-vision.txt`. Created per direct
instruction alongside `z0.zoo_0000` ("we will figure out the user
account association as well in here as z0.user/ or something") -
explicitly a placeholder for a real design pass later, not a build
task for this one. Read this file first if you pick this up; it
captures what's already known so the next pass doesn't start from
zero, same purpose as `z0.zoo_0000/dox/hand-off-muta-eggs.txt`.

## 1. What "user account association" would actually need to mean

Not yet decided, but the real questions any design pass needs to
answer:

- **Ownership**: does a pet/piece belong to a specific user, or is
  "whoever is playing right now" the only identity that exists (today's
  reality in mutaclsym/zoo_0000/egg-pals - none of them have any concept
  of WHO is playing, only WHAT is being played)?
- **Multi-player disambiguation**: if two people use the same machine
  (or the trading/auction platform in mutaclsym's `platform-vision.txt`
  §3 ever gets built), how does the engine know pet A belongs to
  person 1 and pet B to person 2? Right now nothing does - every piece
  in every one of these games is globally visible/controllable by
  whoever is sitting at the keyboard.
- **Where would a user_id actually live?** Candidate hooks, none
  chosen: a field on the piece itself (`owner_user_id=` in state.txt),
  a field on the `trade_envelope.txt` format `z0.zoo_0000/ops/
  pet_export.c` already writes (a natural, minimal extension point -
  see §3 below), or a directory-level scheme (per-user subdirectories
  under `pieces/world_01/`, mirroring how `exchange/` is already a
  shared directory ALL games can see into).
- **Does xlector itself have an owner?** If two people could each run
  their own xlector cursor in the same world (a real multiplayer
  scenario), the single global `active_target_id` field
  `z0.zoo_0000/ops/xlector_input.c` uses today would need to become
  per-user, not a single shared value. Not needed for anything built so
  far (every game in this family is single-player, single-cursor).

None of this is decided. Write down the real answer here once a real
design pass happens, don't guess further in this file.

## 2. Real precedent that already exists, unintegrated

Real 1.TPMOS already has a working, standalone account/session app:
`1.TPMOS_c_+rmmp.0102.0028/projects/user/` - real ops
(`create_profile.c`, `auth_user.c`, `get_session.c`,
`manager/user_manager.c`), a working test profile
(`pieces/profiles/testplayer`), documented in that project's own
`README_OPS.txt` and in `1.TPMOS_c_+rmmp.0102.0028/!.CODEBASE_MAP.txt`
lines 231-243 ("User accounts for all games"). It's meant to be
installed per-project (via Fondu, per `README_OPS.txt` lines 147-161)
and called from any game's PAL scripts as `OP user::create_profile
"player1"`.

**Confirmed via direct research this session: nothing in mutaclsym,
zoo_0000, or egg-pals installs or calls this app.** It's real, working,
sitting there unused by any of this family's actual games. If a real
design pass happens here, this is the starting point to evaluate - not
something to reinvent from scratch - but also not something to adopt
blindly: `1.TPMOS_c_+rmmp.0102.0028/#.wussup-may2.txt` lines 141-152
flags that project's own login/signup UI as genuinely unfinished
("false scaffolding... login does nothing"), so "real and working"
applies to the account/session OPS specifically, not to a
ready-to-use login flow.

## 3. How this could eventually connect to the pet import/export standard

Not built, but the natural extension point once user identity exists:
`z0.zoo_0000/ops/pet_export.c`'s own `trade_envelope.txt` format already
has exactly the shape platform-vision.txt §2 proposed (pipe-delimited
`key|value` rows, origin game + payload fields) - adding an
`owner_user_id|<id>` row there, once real user accounts exist, is a
small, additive change, not a redesign. That would let the shared
`exchange/` directory (currently a single flat free-for-all any game
can read/write) become ownership-aware: an import op could refuse to
pull in a pet whose envelope names a DIFFERENT user, or a future
auction/trading platform (`platform-vision.txt` §3) could use that
same field to know who's offering what.

**Not decided, flagged for whoever designs this for real:** whether
`exchange/` should become per-user subdirectories
(`exchange/<user_id>/<piece_id>/`) at that point, or stay flat with the
ownership field carried only inside each envelope. Both are reasonable;
neither is built.

## 4. Explicitly not started

No ops, no state files, no piece.pdl, nothing under `pieces/` in this
directory yet - this is a docs-only placeholder. The actual
`projects/user/` app referenced in §2 lives entirely in
`1.TPMOS_c_+rmmp.0102.0028/` and has not been copied, adapted, or
wired into anything in this family.
