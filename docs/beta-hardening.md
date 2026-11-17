# Beta correctness regression work

Patch 2 repairs a freshness mismatch: AGE was sorted using the later memory/I/O
observation time, although the displayed AGE value and validity belong to the
CPU/age acquisition. A later memory query could make an old age appear fresh to
the comparator. The native fixture now tests the actual production comparator.

The system-commit helper now publishes byte total, limit and fraction atomically
at the C function boundary: an overflowing second multiplication cannot leave
mixed old/new output values. A malformed action-kind anywhere in a batch is
rejected before any target is dispatched. These are source-level fixes with
focused failure-path regressions, not claims about observed user crash rates.

Additional regression vectors cover 1ms..60s timing, values above 2^53, reversed
read brackets, regressing clocks, cancellation before dispatch and native memory
sorting around 4/8 GiB. Native tests exercise real comparator code, not a copied
algorithm. Windows execution evidence is recorded separately after CI.

Remaining NTop families: domain/GMSA accounts, protected-process coverage, real
multi-group CPUs/hotplug, non-ASCII console presentation and remote disconnects.
No upstream issue is closed merely because equivalent code has been rewritten.
