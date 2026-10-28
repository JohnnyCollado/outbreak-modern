# Online-play work trees, apart from the ones the touch-controls session builds (plan S1/S2, developer 2026-10-26).
# C:\ob\net-up = git worktree of C:\ob\up (branch net-online from 58145be = patch 0080); net-src = its copy plus
# generated files. Source this, then run tools/ob.sh.
export OB_SRC=C:/ob/net-src OB_R=C:/ob/net-r OB_WORK=C:/ob/net-work OB_OVL=C:/ob/net-ovl OB_LOGS=C:/ob/net-logs
export OB_PCSX2_GS_BUILD=C:/ob/net-pcsx2-gs-build OB_PCSX2_VU_BUILD=C:/ob/net-pcsx2-vu-build OB_DIAG_BUILD=C:/ob/net-diag-build
export OB_RUN=D:/NEXTJJEN/repos/outbreak-modern/game_files
export OB_T=C:/ob/net-t
# Machine-local overrides (git-ignored): OB_RUN, OB_VCVARS, OB_PY, OB_GRADLE_JDK for this PC.
if [ -f "$(dirname "${BASH_SOURCE[0]}")/net-env.local.sh" ]; then source "$(dirname "${BASH_SOURCE[0]}")/net-env.local.sh"; fi
