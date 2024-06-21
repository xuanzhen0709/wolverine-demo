# bar-py

a simple python demo for time-series bar data (currently available for futures).

&nbsp;

# checkpoint/checkpoint-py

these two demonstrate the use cases of checkpoints - which allows user implementations to preserve internal state across multiple runs.

&nbsp;

# csmbp/csmbp-py

cross-sectional event driven market-by-price data provided at 3s interval.

provides order/cancel/trade/mbp (snapshot with top 10 levels) data.

this is a preliminary version used for data evaluation and the APIs may not be stable.

&nbsp;

# csstock/csstock-py

cross-sectional 3s snapshot.

&nbsp;

# mbp

similar to csmbp, except that csmbp is the cross-sectional version which packs and sends data in batches (every 3s), and that this time-series version sends per-instrument updates.

for now it only supports full-market subscription - but can be extended to allow for subscribing to individual instruments, without much change to the callback APIs. so the APIs can be considered more stable than those of csmbp.

&nbsp;

# multitickers-py

subscribes to multiple instruments and trades one of them (futures)

&nbsp;

# snapshot-py

single instrument snapshots (futures)
