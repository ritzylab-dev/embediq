# contrib_sim — not yet run

This file is updated by `tests/contrib_sim/run.sh` each time the promotion gate runs.
It records the result of the last fresh-clone simulation against `origin/dev`.
Git history of this file is the audit trail of all promotion gate runs.

Run it before every dev→main promotion:

    bash tests/contrib_sim/run.sh

Then commit the updated file to dev before creating the promotion PR.
