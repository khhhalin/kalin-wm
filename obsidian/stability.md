# Stability

Stability is the primary blocker for kalin-wm v1.0. A stability audit identified
23 tracked issues (4 critical, 8 high, 9 medium, 2 low), all in the Phase 0
checklist, and all now fixed and re-verified against the live code.

The most critical areas were: [[crop-mode]] (division by zero, NULL derefs,
double-free), input handling (NULL keyboard state, `grabc` deref during drag),
layout calculation (division by zero, infinite recursion risk), and client
lifecycle (memory leaks, unchecked allocations). A separate [[spawn]] crash was
also fixed.

The project's coding rule is defensive C: every pointer deref NULL-checked, every
divisor non-zero, every allocation failure handled. The full findings are in
[[research/active-design/stability-audit|the stability audit]]; fixes are
summarized in [[research/active-design/fixes-summary|the fixes summary]].
