# A 股微结构因子研究报告

> 数据源：每 3s 推送一次，含该 3s 内全部逐笔 MBO（Order/Cancel/Trade，带委托号）+ 10 档快照。
> 预测目标：~1 日前向收益（低频），每日 8 个固定 30 分钟时点出值（09:30/10:00/10:30/11:00/13:00/13:30/14:00/14:30，对齐 `stats/mid` 中间价标签 + `compute_label` 前向收益）。
> 生成方式：多智能体 fan-out（8 个微结构视角）+ 对抗式新颖性/可算性评审 + 人工去重精选。导出日期：2026-07-20。

> 因子总数：**71** 个（其中经评审/推荐 **65** 个；其余为同族备选或未评审候选）。

---

## 1. 方法论与设计原则

1. **数据独占性优先**：优先设计只有 3s 逐笔 MBO（尤其 `order_id`/`bid_id`/`ask_id` 生命周期）+ 10 档笔数（`bid_cnt/ask_cnt`）才能算的因子——这是相对只有快照/日频数据的因子库的天然增量来源。
2. **低频稳定化**：所有因子聚合为「截至时点 t 的累积统计」或「全日统计」，横截面可比、低换手，避免高频瞬时噪声；瞬时量一律做时间加权/EWMA 降噪。
3. **机制正交于基线**：刻意避开简单 OBI、Amihud、动量/反转、裸 Kyle-λ、裸 VPIN、主力大单净流入等已知基线；每个因子要么机制新颖，要么对基线做「生命周期化 / 几何化 / 横截面化 / 条件化」的信息量重构。
4. **买卖不对称即信息**：A 股 T+1 + 多数股票禁止日内融券做空 → 日内卖压主要来自存量持仓、买盘为新增需求。绝大多数因子刻意做买/卖侧不对称，正是利用这一制度约束提取信息。
5. **严格无 look-ahead**：日内门控/时长一律用 `exchtime`（当日纳秒），跨日/日志才用 `localtime`（epoch）；任何「冲击后窗 / 恢复窗 / 撤单时 best」必须整体落在 t 之前，或用**前一事件快照**。

**评审判据**：只保留 `novelty≥3 且 computable=true 且 lowfreq≥2`；对抗评审剔除等同基线无新意的、不可算的、纯高频噪声的、有 look-ahead 的，并修补公式漏洞（缺定义/除零/时间基准混用/涨跌停竞价处理/截面去均值）。novelty/lowfreq 为 1–5 自评分。

---

## 2. 因子目录（8 视角）

## 视角一、委托生命周期 / 撤单

*用 order_id 把 Order↔Cancel↔Trade 串成生命周期，刻画诚意 vs 试探/幌骗挂单。*

> 状态：生成候选（未过对抗评审；★=推荐, ○=同族备选） · 本视角 8 个因子

### 1. 存活加权近端深度不平衡 SW-DOBI `Survival-Weighted near-touch Depth OBI` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：简单OBI/多档不平衡把'刚挂0.1秒就会撤的量'和'挂了几分钟没撤的诚意量'一视同仁。真正的买盘累积是耐心、抗撤的。给每笔仍存活的近端挂单按'已存活时长'加权(存活越久越诚意),撤单会自动把短命量剔除,剩下的加权深度反映真实供需。诚意买盘加权深度占优→上涨。
- **所需字段**：MBO: Order(order_id,side,price,qty,类型,exchtime), Trade(bid_id,ask_id,qty), Cancel(被撤order_id); Snapshot: bp[0],ap[0],exchtime
- **计算公式**：每标的跨事件维护LiveOrders字典 order_id→{side,price,qty0,filled_qty,arrival_exchtime}。逐笔:Order(限价)建记录;Trade按bid_id/ask_id累加filled_qty;Cancel标记removed。t时刻对所有ALIVE限价单,若在最优价K=5档内(买p_o>=bp[0]-K*0.01;卖p_o<=ap[0]+K*0.01):age_o=t-arrival(秒),remaining_o=qty0-filled_qty,权重g_o=1-exp(-age_o/θ),θ≈30s。GBuy=Σ g_o*remaining_o(买),GSell同卖。F1=(GBuy-GSell)/(GBuy+GSell+eps)。
- **聚合到 30min/日频**：每3s事件增量更新LiveOrders与filled_qty(仅用exchtime<=t的逐笔);到达报出时点t时对当前ALIVE集合快照求GBuy/GSell,age用t-arrival现算。全程无look-ahead。
- **横截面标准化 / 中性化**：全A股同时点winsorize(1%/99%)后z-score或rank到[-0.5,0.5];对裸OBI((total_bid_vol-total_ask_vol)/(total_bid_vol+total_ask_vol))做横截面回归取残差,保留纯'存活溢价'增量;再行业中性。
- **预期方向**：+
- **新颖性**：基线OBI是瞬时量的静态不平衡;此因子用生命周期存活时长对深度做几何加权,把'诚意度'内生化,是对OBI的lifecycle化重构而非裸OBI。
- **A股陷阱 / look-ahead**：涨跌停锁死标的的巨量排队age极长且无法成交→剔除当日触板标的;集合竞价残留单存活时间人为超长→从连续竞价09:30起算或单独处理开盘保留量;SSE/SZSE order_id体系与逐笔机制不同,匹配前分市场;look-ahead:age与remaining只用exchtime<=t的Trade/Cancel。

### 2. 极短命委托买卖不对称 FOA `Fleeting Order Asymmetry` ★推荐
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：挂上即撤、零成交的极短命委托是试探/spoof/高频晃点的典型痕迹。若卖侧极短命量远多于买侧,说明卖压多为'虚晃一枪即撤回'的假供给,真实需求在其后→上涨;买侧极短命多则是假需求(拉抬掩护)→下跌。
- **所需字段**：MBO: Order(order_id,side,qty,类型,exchtime), Cancel(被撤order_id,side,qty), Trade(用于确认filled_qty==0)
- **计算公式**：fleeting单:终局==CANCEL 且 filled_qty==0 且 lifespan<τ(τ=3s,可调至1s)。FleetBuyVol=Σ remaining of fleeting buy;FleetSellVol同卖。PlacedBuyVol/PlacedSellVol=当日截至t各侧限价单报单总qty。标准化撤单率 fbr_buy=FleetBuyVol/PlacedBuyVol,fbr_sell同理。F2=fbr_sell - fbr_buy。
- **聚合到 30min/日频**：每窗口匹配Order→Cancel(被撤order_id),命中且filled==0且时差<τ即计入对应侧fleeting累加器;PlacedVol按侧累加所有限价Order.qty。仅用exchtime<=t。
- **横截面标准化 / 中性化**：横截面rank标准化;因fleeting强度随活跃度膨胀,已用同侧PlacedVol归一后再横截面比较;对总撤单率或换手做残差化去除纯活跃度成分。
- **预期方向**：+
- **新颖性**：基线'简单撤单比'不分存活时长、不分零成交、不做买卖不对称。此处专挑'零成交超短命'这一spoof指纹并做side-asymmetry,是撤单比的生命周期化+条件化。
- **A股陷阱 / look-ahead**：09:20-09:25集合竞价禁撤→fleeting只能出现在09:15-09:20与连续竞价,须排除开盘竞价段避免制度性伪信号;做市/合法改价(撤后立即同侧改价重挂)会误判spoof,可加'撤后τ内同侧同价无补单'加强;T+1使融券受限,卖压主来自存量,fleeting卖更可能是真实犹豫而非机构spoof,故τ宜小并结合F8;look-ahead:lifespan只用<=t事件。

### 3. 大额委托撤单倾向不对称 LOCA `Large-Order Cancel-propensity Asymmetry` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：大额诚意买单会耐心持有等成交;而大额卖单若频繁撤销,多为掩护/压价试探(挂大卖墙吓人再撤)。大额买撤率低、大额卖撤率高→真实需求强→上涨。
- **所需字段**：MBO: Order(order_id,side,qty,类型), Cancel(被撤order_id,side,qty)
- **计算公式**：大额阈值用'该标的当日该侧单笔qty分布的90分位' q90_side(size-comparable,避免大盘股天然单大)。大单集合 LB={buy orders qty0>q90_buy}, LS同卖。LOCR_buy=撤销(终局==CANCEL)的大买单笔数/LB总笔数(或用remaining量加权);LOCR_sell同理。F3=LOCR_sell - LOCR_buy。
- **聚合到 30min/日频**：累积统计当日截至t各侧单笔qty估q90(用前一日或当日已见在线分位数,避免未来单);逐笔判定大单并累加placed与cancelled计数/量。仅用<=t。
- **横截面标准化 / 中性化**：阈值per-stock自适应已保证横向可比;F3横截面winsorize+z-score;可对bid_cnt/ask_cnt推得的平均单笔量做中性化。
- **预期方向**：+
- **新颖性**：基线'大单净流入'按成交额切大单净额(成交视角);此处从'挂单-撤单'意图视角看大额委托的撤单倾向不对称,捕捉未成交的意图信息,机制正交于成交型大单因子。
- **A股陷阱 / look-ahead**：机构真实派发也会大单撤改(冰山/拆单),per-stock阈值+看是否零成交可缓解;涨跌停排队大单不可撤,剔除;q90须在线用历史/已见数据,不能用当日全量(look-ahead);SSE/SZSE分开。

### 4. 临成交前逃单不对称 FoApproach `Flee-on-Approach Asymmetry` ★推荐
**novelty 5/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：诚意挂单在价格逼近、即将成交时会留下成交;非诚意(试探/spoof)则在'快被吃到'的瞬间撤退。买单在best ask下探贴近其价位时才撤=假买;卖单在best bid上探贴近时撤=假卖。卖侧逃单多、买侧逃单少→真实买盘敢承接→上涨。
- **所需字段**：MBO: Order,Cancel(被撤order_id,side,price),Trade(price,qty); Snapshot: bp[0],ap[0](兜底), exchtime
- **计算公式**：对每笔终局==CANCEL的单o,取撤单时刻的running best(由MBO重建,或撤单前最近快照bp[0]/ap[0])。买单逃单判定:(ap[0]-p_o)<=ε*0.01 或 撤单前Δ内有Trade价<=p_o+ε*0.01(ε=1~2 ticks),即成交已逼近仍撤。FleeRate_buy=逃单买/撤单买总数;FleeRate_sell对称(用bp[0]-p_o<=ε)。F4=FleeRate_sell - FleeRate_buy。
- **聚合到 30min/日频**：维护MBO重建的running best;每次Cancel命中时比对当时best与p_o,打逃单标签并累加。仅用<=t。
- **横截面标准化 / 中性化**：横截面rank;逃单率是比例天然可比;对价差(ap[0]-bp[0])做中性化(窄价差更易触发逼近)。
- **预期方向**：+
- **新颖性**：把撤单的'时机'相对价格逼近条件化,识别'诚意vs逃避'——现有撤单因子几乎都不带撤单时点与best的相对几何,机制新颖(生命周期×时序)。
- **A股陷阱 / look-ahead**：关键防look-ahead:逼近判定只能用撤单时刻及之前的best/Trade,严禁用撤单之后是否真成交来定义;3s快照粒度粗→优先用MBO重建best而非快照;做市合法闪避会计入,但正是要捕捉的非诚意行为;涨跌停、竞价段剔除。

### 5. 被吃后同价补单速度/吸收不对称 Refill `Post-Hit Absorption Asymmetry` ★推荐
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：诚意买盘在被主动卖砸穿某价位后会迅速在同价或更优价补单(队列重建=吸收抛压),体现存量抛压被真实需求接住。买侧补单快而厚、卖侧补单慢→上涨。
- **所需字段**：MBO: Trade(price,qty,主动方side,bid_id,ask_id), Order(side,price,qty,exchtime); Snapshot: bp[0]/ap[0]定价位
- **计算公式**：识别'被吃'事件:Trade主动方==卖且命中bid挂单(消耗bid价位L的量ConsumedBid)。补单:事件后ΔT(=6s或2窗口)内在价位>=L的新买Order量ReplBuy,补单延迟latency=首笔补单exchtime-被吃exchtime。RefillRatio_bid=ΣReplBuy/ΣConsumedBid;RefillSpeed_bid=1/mean(latency)。卖侧(主动买吃ask)对称得RefillRatio_ask。F5=zscore(RefillRatio_bid)-zscore(RefillRatio_ask)。
- **聚合到 30min/日频**：逐笔标记被吃价位与量,开一个短时窗匹配后续同价/更优补单;累积ratio与latency的稳健均值。被吃事件须早于t-ΔT才计入,避免删失。
- **横截面标准化 / 中性化**：ratio已归一;横截面rank;对Amihud/换手中性化(高流动性天然补单快)。
- **预期方向**：+
- **新颖性**：把'队列被消耗→补单'这一生命周期闭环量化为吸收速率,而非静态深度;区别于所有静态盘口/OBI/深度因子,刻画动态韧性。
- **A股陷阱 / look-ahead**：补单窗ΔT须完全在t之前完成(被吃事件要早于t-ΔT);自然排队再挂vs真实信念补单难分,用'量>被吃量的显著补'过滤;T+1使卖压=存量,买侧吸收信号尤其有意义;涨跌停价位补单是排队非吸收,剔除。

### 6. 委托存活时长分布位置不对称 Lifespan `Order Lifespan Location Asymmetry` ○同族备选
**novelty 3/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：耐心是诚意的信号。买方限价单整体存活时长(对数中位数)显著长于卖方,说明买家愿意等待、慢慢吸筹;卖方挂单来去匆匆则供给不坚定→上涨。
- **所需字段**：MBO: Order(order_id,side,类型,exchtime), Cancel(被撤order_id), Trade(bid_id,ask_id); Snapshot: bp[0],ap[0](定近端), exchtime
- **计算公式**：对当日截至t所有近端限价单(K档内),lifespan L_o(CANCEL/FILL用实际,ALIVE用t-arrival右删失,两侧一致处理)。med_logL_buy=median(log(1+L_o)) over buy;med_logL_sell同卖。F6=med_logL_buy - med_logL_sell。
- **聚合到 30min/日频**：在线维护两侧log-lifespan的分位数(如t-digest);终局或删失时入池。仅用<=t事件。
- **横截面标准化 / 中性化**：横截面z-score;建议对F1正交化(F1是深度加权量、F6是流的时长分布位置,机制相关需去共线),取残差。
- **预期方向**：+
- **新颖性**：把存活时长作为分布对象按side刻画(而非撤单率标量),生命周期视角的一阶新颖度;基线无任何存活时长因子。
- **A股陷阱 / look-ahead**：右删失两侧必须一致否则系统性偏差;开盘竞价残留单存活人为超长,从连续竞价起算;lifespan只用<=t事件构造。

### 7. 贴近度加权撤单强度不对称 WCI `Aggressiveness-Weighted Cancel Imbalance` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：撤在最优价附近的单才真正抽走可成交流动性,深档撤单意义小。给撤单按'撤时距best的贴近度×剩余量'加权:买侧近端撤单少=真实买盘稳、卖侧近端撤单多=供给撤离→上涨。是对'加权撤单率'的几何化实现。
- **所需字段**：MBO: Order(price,qty,side,类型), Cancel(被撤order_id,side,price,qty), Trade(定filled); Snapshot: bp[0]/ap[0]兜底, exchtime
- **计算公式**：对每笔CANCEL单o(remaining_o=qty0-filled_qty),撤时距best距离 d_o(ticks,买用bp[0]-p_o,卖用p_o-ap[0]),贴近权 h_o=exp(-d_o/λ)(λ≈3 ticks)。WCancel_buy=Σ h_o*remaining_o(买);WPlaced_buy=Σ over 报单 h*qty0(同权基)。ci_buy=WCancel_buy/WPlaced_buy,ci_sell同理。F7=ci_sell - ci_buy。
- **聚合到 30min/日频**：用running best给每笔撤单/报单打h;累加WCancel与WPlaced。撤时best用重建值(撤单当时),不用t时刻best。
- **横截面标准化 / 中性化**：ci为比例可比;横截面rank;对F2(fleeting)与F3(大单)正交化,避免三个撤单类因子共线,保留贴近度独有成分。
- **预期方向**：+
- **新颖性**：基线撤单比是无权计数;此因子按撤单相对best的几何贴近度与剩余量加权,聚焦'真正抽走可成交流动性'的撤单,是撤单比的几何化+信息量重构。
- **A股陷阱 / look-ahead**：涨跌停时best异常导致d_o失真,剔除;竞价段禁撤剔除;h必须用撤单当时的best(重建),不可用t时刻best(轻微look-ahead)。

### 8. 深档假墙撤离(掩护试探)不对称 CoverWall `Deep Spoof-Wall Withdrawal Asymmetry` ★推荐
**novelty 5/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：掩护/试探手法:在离最优价数档的深处挂大单造成'厚墙'威慑,待价格朝反方向走或对手盘反应后撤回,自身几乎不成交。深卖墙(假供给)被大量撤离=压制解除→上涨;深买墙(假需求)撤离=拉抬掩护结束→下跌。
- **所需字段**：MBO: Order(order_id,side,price,qty,类型), Cancel(被撤order_id,side,qty), Trade(bid_id,ask_id定filled); Snapshot: bp[0]/ap[0], exchtime
- **计算公式**：cover单判定:qty0>q75_side(大) 且 到达时距best D∈[3,9] ticks(深) 且 终局==CANCEL 且 filled_qty==0(几乎未成交)。DeepCoverSell=Σ remaining of 卖侧cover;DeepCoverBuy同买。归一 dc_sell=DeepCoverSell/(深卖侧报单总量),dc_buy同理。F8=dc_sell - dc_buy。
- **聚合到 30min/日频**：报单时用running best算δ_o与大小标签存入记录;撤单且零成交时按标签累加cover量。q75在线估计,仅用<=t。
- **横截面标准化 / 中性化**：dc为比例可比;横截面rank;与F2/F3正交化(F2近端超短命、F3不限深度、F8专注深档大单假墙),保留'深档假墙'独有维度。
- **预期方向**：+
- **新颖性**：直接建模A股常见spoofing/掩护墙的完整生命周期指纹(深+大+零成交+撤离)并做买卖不对称,机制高度新颖,基线库无对应。
- **A股陷阱 / look-ahead**：合法深档流动性管理会误判,但零成交+大+深三重条件已强过滤;融券受限使真正卖方spoof动机不同,深卖墙也可能是真实限价供给,须结合F5/F1交叉确认;δ_o与大小阈值都须用<=t信息(q75在线估计);涨跌停、竞价段剔除;SSE/SZSE逐笔字段差异分市场校准。

---

## 视角二、新委托下单激进度

*用价差归一化几何给到达的委托流打分，捕捉下单意图（含未成交/被撤），领先成交 tape。*

> 状态：已对抗式评审精选 · 本视角 10 个因子

### 9. 新委托下单激进度买卖不对称 `Order Placement Aggressiveness Asymmetry (OPA-Asym)`
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：Signs the ARRIVING limit-order flow by spread-normalized distance toward the opposite touch, capturing intent (including later-cancelled/unfilled orders) rather than the resting book (OBI) or realized trades. Impatient/informed demand posts close to or across the touch; forced/patient supply parks deep.
- **所需字段**：MBO Order: side, price, qty, exchtime. Snapshot of PREV event: bp[0], ap[0].
- **计算公式**：Gate to continuous session by exchtime (time-of-day): accumulate only orders whose event exchtime in [09:30:00,11:30:00] U [13:00:00,14:57:00]; exclude 09:15-09:30 open auction and 14:57-15:00 close auction. For each new Order o in event k use the PREV-event snapshot bp0_prev,ap0_prev (never event-k own snapshot -> look-ahead). spread_prev=max(ap0_prev-bp0_prev,0.01). Buy aggressiveness a_o=(p_o-bp0_prev)/spread_prev; sell a_o=(ap0_prev-p_o)/spread_prev (0=join own best, 1=at opposite touch, >1=cross, <0=behind own touch). Clip a_o to [-3,3]. Per-event qty-weighted sums: AB_k=sum_{buy}q_o*a_o, QB_k=sum_{buy}q_o; AS_k,QS_k for sells. As-of-t factor OPA_Asym(t)=(sum_{k<=t}AB_k/max(sum QB_k,1))-(sum AS_k/max(sum QS_k,1)). Daily variant uses full continuous session.
- **聚合到 30min/日频**：Event(3s): qty-weighted a_o sums per side (AB_k,QB_k,AS_k,QS_k). Timepoint: cumulative from 09:30 to t -> as-of-t ratio (smooth, low turnover). Daily: full continuous-session accumulation. Auction windows excluded via exchtime.
- **横截面标准化 / 中性化**：At each of the 8 timepoints: winsorize 1/99, then z-score or rank cross-sectionally; residualize against log free-float mktcap, log(turnover accumulated to t), and average spread level (aggressiveness is mechanically higher on wide-spread/illiquid names).
- **预期方向**：Positive: buyers submitting more aggressively (closer to/past opposite touch) than sellers -> higher 1-day forward return.
- **新颖性**：Continuous spread-normalized geometry of INCOMING placement intent, distinct from resting-book OBI and from executed-trade flow; counts unfilled/cancelled intent; not the binary big/small main-force net-inflow baseline.
- **A股陷阱 / look-ahead**：MUST use prev-event snapshot (event-k snapshot post-dates its own orders). Keep buy/sell sides separate: under T+1+no-short, sell aggression is largely forced holder liquidation so the asymmetry is informative. Limit-up/down: degenerate spread and one-sided book -> drop names locked in prev snapshot (av0==0 at +limit or bv0==0 at -limit) and rely on the [-3,3] clip. Use exchtime (time-of-day ns) for gating, never localtime (epoch).

### 10. 跨价意愿（可成交委托强度） `Cross-Price Willingness (Marketable Order Intensity)`
**novelty 3/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：Fraction of arriving order VOLUME that is immediately marketable (crosses the spread at submission) = raw demand for immediacy measured at ORDER time, leading the realized aggressor-trade tape. Buy immediacy = fresh conviction; sell immediacy under T+1 = holders paying to exit.
- **所需字段**：MBO Order: side, price, qty, exchtime. Snapshot of PREV event: bp[0], ap[0].
- **计算公式**：Continuous-session gating as F1. Buy marketable iff p_o>=ap0_prev; sell marketable iff p_o<=bp0_prev (prev-event snapshot). Per event MB_k=sum_{buy,p_o>=ap0_prev}q_o, TB_k=sum_{buy}q_o; MS_k,TS_k for sells. mbuy(t)=sum_{k<=t}MB_k/max(sum TB_k,1), msell(t)=sum MS_k/max(sum TS_k,1). Factor CrossWill(t)=mbuy(t)-msell(t) (magnitude variant: mbuy(t) alone).
- **聚合到 30min/日频**：Event: marketable vs total qty per side (MB_k,TB_k,MS_k,TS_k). Timepoint: cumulative from open to t. Daily: full session. Auctions excluded via exchtime.
- **横截面标准化 / 中性化**：Rank cross-sectionally at each timepoint (bounded but heavy-tailed); neutralize log(turnover to t), spread level, realized volatility. Residualize against F1 to remove the shared crossing-region overlap.
- **预期方向**：Positive: high buy crossing share and buy>sell -> higher forward return.
- **新颖性**：Measures immediacy demand from the ORDER side (submitted crossing volume incl. later-cancelled intent) so it leads realized aggressor-imbalance / VPIN which only see executed tape. Downgraded to 3 because the crossing region (a_o>1) overlaps F1; retained as the more robust binary/threshold version and residualized against F1.
- **A股陷阱 / look-ahead**：Cancelled crossing orders still count here (captures intent but exposes flicker/spoof contamination) -> pair with the Genuine-Aggression lifecycle factor and cap per-order qty at a rolling high percentile. SSE market-order handling is version-dependent; prefer inferred crossing from price vs prev touch. Prev snapshot mandatory. Limit board: any order 'crosses' a locked book -> exclude locked names. Auctions excluded via exchtime.

### 11. 价内改善报价占比（不跨价插队） `Inside-Spread Price-Improvement Share`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：Orders placed strictly inside the spread improve the touch without crossing -> competitive liquidity provision / stepping-ahead that signals directional lean, invisible to both OBI (resting book) and trade-flow measures.
- **所需字段**：MBO Order: side, price, qty, exchtime. Snapshot of PREV event: bp[0], ap[0].
- **计算公式**：Restrict to eligible events with spread_prev=ap0_prev-bp0_prev>0.01 (interior exists). Buy improving iff bp0_prev<p_o<ap0_prev; sell improving iff bp0_prev<p_o<ap0_prev (order lands strictly inside without crossing). Per eligible event PIB_k=sum improving-buy q_o, TBe_k=sum buy q_o; PIS_k,TSe_k for sells. PIshare_buy(t)=sum_{k<=t}PIB_k/max(sum TBe_k,1); PIshare_sell analogous. Factor PI_asym(t)=PIshare_buy(t)-PIshare_sell(t). Report per-name coverage = fraction of session with spread_prev>1 tick.
- **聚合到 30min/日频**：Event: improving vs total buy/sell qty over eligible events with spread_prev>1 tick (PIB_k,TBe_k,PIS_k,TSe_k). Timepoint: cumulative. Daily: full session. Auctions excluded.
- **横截面标准化 / 中性化**：Rank cross-sectionally WITHIN the sub-universe whose median spread>1 tick (report coverage; NaN elsewhere); neutralize spread level and turnover.
- **预期方向**：Positive: higher net buy inside-spread price-improvement -> higher forward return.
- **新颖性**：Isolates the queue-jump-but-not-cross behavioral regime, only visible with MBO + reference book; neither resting-book imbalance nor trade-based flow can observe it.
- **A股陷阱 / look-ahead**：Many liquid A-shares lock at 1-tick spread => empty interior => low coverage; restrict universe and treat as coverage-gated (this caps lowfreq at 3). Prev snapshot mandatory. Auctions excluded via exchtime. At limit board no interior exists.

### 12. 算法拆单强度（子单簇指纹） `Algorithmic Slicing Intensity (child-order size-repetition footprint)`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：VWAP/TWAP/iceberg parent orders emit many same-side child orders of near-identical size -> a machine fingerprint. Heavy repeated-size mass on the buy side = hidden institutional accumulation; on sell side = programmatic distribution.
- **所需字段**：MBO Order (individual per-order records required): side, price, qty, order_id, exchtime.
- **计算公式**：Continuous-session gating. Keep non-retail orders q_o>=Q_min per side, Q_min=max(2000 shares, stock 60th-pct rolling order size) to avoid retail 100/500 rounding. Quantize q to nearest 100 shares. For buys up to t: n(q)=count of qualifying buys of exact quantized size q; V(q)=q*n(q). RepShare_buy(t)=sum_{q:n(q)>=3}V(q)/max(sum_all V(q),1). Sells analogous. Factor Slice_asym(t)=RepShare_buy(t)-RepShare_sell(t).
- **聚合到 30min/日频**：Event: update per-side quantized size histograms with each new qualifying order. Timepoint: histogram cumulative from open to t -> RepShare. Daily: full session.
- **横截面标准化 / 中性化**：Cross-sectional z or rank; regress out log(#qualifying orders to t) (repetition rises mechanically with count) and turnover.
- **预期方向**：Positive: higher buy-side same-size repetition and buy>sell -> higher forward return.
- **新颖性**：Fingerprints algo child-order STRUCTURE via order-level size repetition -- an intent-structure signal far beyond baseline large-trade net-inflow which only thresholds trade notional and cannot see slicing.
- **A股陷阱 / look-ahead**：Retail round lots -> Q_min gate essential. A single whale is not slicing (require n(q)>=3). No look-ahead: only orders arriving up to t. REQUIRES engine to enumerate individual Order events with per-order qty (NOT computable from 3s-window-aggregated add volume) -> confirm CsMboEvent exposes individual Order records before production.

### 13. 规模-激进度耦合（大单激进溢价） `Size-Aggression Coupling (Aggressive-Order Size Premium)`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：Conviction signature: do the LARGE new orders also arrive MORE aggressively? Informed flow is simultaneously big and impatient; noise flow decouples size from aggression.
- **所需字段**：MBO Order: side, price, qty, exchtime. Snapshot of PREV event: bp[0], ap[0].
- **计算公式**：Continuous-session gating. Compute a_o as in F1 (prev snapshot, clipped [-3,3]). Per-stock trailing threshold Qhi=80th pct of buy order size (rolling, no look-ahead). mean_a_large_buy(t)=qty-weighted mean a_o over buys with q_o>=Qhi up to t; mean_a_small_buy(t)=qty-weighted mean over q_o<Qhi. Factor ASP_buy(t)=mean_a_large_buy(t)-mean_a_small_buy(t). Robust variant: qty-weighted cov(a_o, log q_o) over buys. Symmetric sell version available; primary use buy-side (T+1 asymmetry).
- **聚合到 30min/日频**：Event: accumulate a_o and q_o per order tagged large/small vs trailing threshold. Timepoint: cumulative qty-weighted means from open to t. Daily: full session.
- **横截面标准化 / 中性化**：Cross-sectional rank (difference-of-means is noisy) with a min-N gate on the large bucket (else NaN/median-fill); neutralize spread level and turnover.
- **预期方向**：Positive: large new orders arriving more aggressively (big AND impatient) -> higher forward return.
- **新颖性**：Explicitly measures the INTERACTION of size and aggression; baselines treat size (large-order flow) and aggression (OBI/lambda) as separate axes and miss their coupling, the true informed fingerprint.
- **A股陷阱 / look-ahead**：Needs enough large orders per window (thin names unstable -> min-N gate). Trailing/rolling threshold to avoid look-ahead. Prev snapshot for a_o. Limit board -> clip a_o and drop locked names.

### 14. 显式即时性委托类型强度（深交所委托类型） `Explicit Immediacy Order-Type Intensity (SZSE order type)`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：Reads the declared order-type flag directly: 市价 and 对手方最优 are explicit immediacy demand; 本方最优/限价 are passive. A declared-intent measure orthogonal to price-inferred aggression (F1/F2).
- **所需字段**：MBO Order: side, qty, 委托类型 (order type flag; SZSE / ChiNext), exchtime.
- **计算公式**：Continuous-session gating. Classify order type: aggressive_type={市价(market), 对手方最优(opposite-best)}; passive_type={限价(limit), 本方最优(same-best)}. Per event AggTypeB_k=sum_{buy, aggressive_type}q_o, TB_k=sum_{buy}q_o; AggTypeS_k,TS_k for sells. TypeAggr_buy(t)=sum_{k<=t}AggTypeB_k/max(sum TB_k,1); sell analogous. Factor Type_asym(t)=TypeAggr_buy(t)-TypeAggr_sell(t). Set NaN where 委托类型 field not exposed.
- **聚合到 30min/日频**：Event: aggressive-type vs total qty per side (SZSE order-type flag). Timepoint: cumulative. Daily: full session. Auctions excluded.
- **横截面标准化 / 中性化**：Rank WITHIN the exchange-eligible sub-universe (SZSE incl. ChiNext); do NOT pool SSE+SZSE (field availability differs); neutralize turnover.
- **预期方向**：Positive: higher net buy explicit-immediacy order-type share -> higher forward return.
- **新颖性**：Uses the raw order-type field most factor libraries drop entirely; cleanly separates DECLARED immediacy from price-inferred immediacy, orthogonal to spread-geometry factors.
- **A股陷阱 / look-ahead**：SSE historically lacks these type codes / has only partial 市价 support -> coverage limited to SZSE, version-dependent (this caps lowfreq to 3 for a full-universe model; NaN elsewhere). No look-ahead. Auctions excluded via exchtime.

### 15. 新委托挂单深度（离盘口报价距离） `New-Order Placement Depth (ticks behind touch)`
**novelty 3/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：Among PASSIVE new orders, how many ticks behind best they sit. Eager demand adds liquidity right at the touch; weak/patient demand parks deep. Describes where liquidity is ADDED, not where it rests, and works beyond the visible 10 levels since price is known.
- **所需字段**：MBO Order: side, price, qty, exchtime. Snapshot of PREV event: bp[0], ap[0].
- **计算公式**：Continuous-session gating. Consider only PASSIVE new orders: passive buy iff p_o<=bp0_prev, passive sell iff p_o>=ap0_prev (prev snapshot). Buy depth d_o=(bp0_prev-p_o)/0.01 ticks (0=at own best), clip [0,50]; sell depth d_o=(p_o-ap0_prev)/0.01, clip [0,50]. Per event DB_k=sum_{passive buy}q_o*d_o, PQB_k=sum_{passive buy}q_o; DS_k,PQS_k for sells. PlaceDepth_buy(t)=sum DB_k/max(sum PQB_k,1); sell analogous. Factor Depth_asym(t)=PlaceDepth_sell(t)-PlaceDepth_buy(t) (buyers adding closer -> positive).
- **聚合到 30min/日频**：Event: depth-weighted qty per side for PASSIVE new orders (DB_k,PQB_k,DS_k,PQS_k). Timepoint: cumulative. Daily: full session. Auctions excluded.
- **横截面标准化 / 中性化**：Cross-sectional z or rank; neutralize spread level and nominal (tick-relative) price -- low-price stocks have coarser relative ticks; winsorize.
- **预期方向**：Positive: passive buyers adding closer to touch than passive sellers -> higher forward return.
- **新颖性**：Depth distribution of ADDED liquidity (placement geometry of new passive orders), distinct from resting-book shape and from crossing/interior share (F2/F3); uses price directly so measurable below level-10.
- **A股陷阱 / look-ahead**：Prev snapshot mandatory. Orders below level-10 still measurable via tick distance; clip at 50 ticks. Limit board -> huge distances -> clip and/or drop locked names. Neutralize nominal price. Auctions excluded via exchtime. Downgraded to novelty 3: solid but closest of the kept set to a book-geometry restatement.

### 16. 主动单扫单深度（多档成交足迹） `Aggressor Sweep Depth (multi-level fill footprint via id-matching)`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：Reconstruct each parent aggressor order from the trade stream via id-matching and measure how many price levels it swept. A single order chewing through several levels = urgency backed by information (willing to pay slippage).
- **所需字段**：MBO Trade (CsMboEvent TradeFldType): Price, Qty, Side, BidId, AskId; event exchtime.
- **计算公式**：Continuous-session gating. For each Trade, the aggressor parent id = BidId if aggressor Side=buy, AskId if aggressor Side=sell. Group all within-day trades by aggressor id. Per aggressor order g: Qg=sum Qty, span_g=(maxPrice_g-minPrice_g)/0.01 ticks OR #distinct fill prices (prefer #distinct levels to reduce tick-scale dependence). Buy-aggressor accumulate SB=sum_g Qg*span_g, WB=sum_g Qg (over g completed up to t); SS,WS for sell-aggressor. BuySweep(t)=sum SB/max(sum WB,1); SellSweep analogous. Factor Sweep_asym(t)=BuySweep(t)-SellSweep(t).
- **聚合到 30min/日频**：Event: group within-window trades by aggressor parent id, compute per-order sweep span. Timepoint: cumulative span-weighted sums from open to t. Daily: full session.
- **横截面标准化 / 中性化**：Cross-sectional rank; neutralize spread level (wide-spread names span more ticks/level -> prefer #distinct-levels metric) and turnover.
- **预期方向**：Positive: deeper buy-aggressor sweeps and buy>sell -> higher forward return.
- **新颖性**：Uses BidId/AskId to group tape into per-parent-aggressor sweeps and measures multi-level consumption per order -- a genuine lifecycle statistic; Kyle lambda / VPIN aggregate trades without reconstructing per-order sweep depth.
- **A股陷阱 / look-ahead**：Realized within-window trades = no look-ahead. Iceberg refills may reuse/rotate ids differently across exchanges -> validate id semantics before pooling. Limit board caps sweep prices -> clip. Neutralize nominal price / spread. Group only trades whose parent id first appears up to t to keep as-of consistency.

### 17. 即时性需求对可用深度比（激进度除以流动性） `Immediacy Demand vs Available Depth`
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：Marketable arriving order volume relative to the contemporaneously visible opposite-side depth: does incoming immediacy demand overwhelm liquidity actually posted? Distinguishes crossing into a wall vs into an empty book.
- **所需字段**：MBO Order: side, price, qty, exchtime. Snapshot of PREV event: av[0..4], bv[0..4], bp[0], ap[0].
- **计算公式**：Continuous-session gating. Marketable buy qty per event MB_k (as F2, using prev-event ap0_prev). Opposite ask depth from PREV snapshot AskDepth_k=sum av[0..4]. Time-avg AvgAsk(t)=mean_{k<=t}AskDepth_k (guard >0). DemandPressure_buy(t)=(sum_{k<=t}MB_k)/max(AvgAsk(t),eps). Sell side uses MS_k and BidDepth_k=sum bv[0..4]. Factor Press_asym(t)=DemandPressure_buy(t)-DemandPressure_sell(t).
- **聚合到 30min/日频**：Event: marketable qty and top-5 opposite depth (prev snapshot). Timepoint: cumulative marketable qty / time-avg depth from open to t. Daily: full session. Auctions excluded.
- **横截面标准化 / 中性化**：Cross-sectional rank (heavy-tailed ratio); winsorize 1/99; neutralize turnover and realized volatility.
- **预期方向**：Positive: buy immediacy demand exceeding available opposite depth (buy>sell) -> higher forward return.
- **新颖性**：Normalizes submitted aggression by the depth actually available to absorb it; raw marketable-share (F2) and baseline OBI ignore this demand/supply ratio.
- **A股陷阱 / look-ahead**：Classify MB_k AND take depth from the PREV-event snapshot (strict no look-ahead). Top-5 depth averaging smooths spoofable single-level depth. Limit board: opposite depth ~0 -> ratio blows up -> cap and/or exclude locked names. Auctions excluded via exchtime.

### 18. 真实激进度（跨价委托成交/撤单生命周期） `Genuine vs Fleeting Aggression Ratio (fill/cancel lifecycle of crossing orders)`
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：F2/F1 count submitted crossing intent INCLUDING spoofed/withdrawn flicker. This conditions on realized outcome: what fraction of declared immediacy actually executed vs was pulled. High genuine buy aggression = real conviction; high cancelled-crossing share = illusory/manipulative demand. Cleans the intent factors.
- **所需字段**：MBO Order: order_id, side, price, qty, exchtime. MBO Cancel: 被撤 order_id, side, price, qty. MBO Trade: Qty, Side, BidId, AskId. Snapshot of PREV event: bp[0], ap[0].
- **计算公式**：Continuous-session gating. Identify crossing/marketable new orders (buy: p_o>=ap0_prev; sell: p_o<=bp0_prev) by order_id. Track each such order_id across the day's MBO: filled qty fq_o = sum of Trade Qty where BidId==order_id (buy) or AskId==order_id (sell); cancelled qty cq_o = qty appearing in Cancel stream for that order_id. GenuineBuy(t)=sum_{crossing buys<=t} fq_o / max(sum q_o,1). Factor GenAggr_asym(t)=GenuineBuy(t)-GenuineSell(t). Companion spoof proxy FleetingCancel_buy(t)=sum cq_o (crossing buys) / max(sum q_o,1).
- **聚合到 30min/日频**：Event: tag crossing/marketable new orders by order_id. Timepoint: cumulative filled-qty / submitted-qty over crossing orders whose lifecycle resolves by t. Daily: full session. Auctions excluded.
- **横截面标准化 / 中性化**：Cross-sectional rank at each timepoint; residualize against F2 (CrossWill) so it carries only the genuine-vs-fleeting component; neutralize turnover, spread level.
- **预期方向**：Positive: higher share of buy crossing intent that actually EXECUTES (vs is cancelled) and buy>sell -> higher forward return.
- **新颖性**：Lifecycle-conditioned aggressiveness via order_id -> Trade/Cancel matching; separates GENUINE from FLEETING immediacy. Neither the executed-trade tape (misses submitted intent) nor submitted-intent factors (miss the withdrawal) can do this. NEW factor added by reviewer to fix F2's spoof-contamination weakness.
- **A股陷阱 / look-ahead**：Partial fills: attribute executed qty by id, remainder to cancel/rest. Iceberg id-reuse/rotation -> validate id semantics per exchange. REQUIRES Order + Cancel + Trade streams all exposed (confirm engine emits Cancel and per-order Order events). Under T+1 sell-side crossing is largely forced exit -> keep sides separate. 09:20-09:25 no-cancel auction window and all auctions excluded via exchtime. Limit board excluded.

---

## 视角三、知情成交流 / 毒性

*在裸 OFI/VPIN 之外，用符号序列结构、扫穿档深、逆选择条件化刻画知情足迹。*

> 状态：生成候选（未过对抗评审；★=推荐, ○=同族备选） · 本视角 8 个因子

### 19. 成交量加权主动成交符号持续性(拆单足迹) `Volume-weighted aggressor-sign persistence` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：知情资金把母单拆成大量同向主动子单(metaorder splitting),表现为主动成交符号在时间上的正自相关,独立于净不平衡幅度。两只净OFI相同的股票,一只来自一个持续单向战役(高自相关),一只来自众多独立噪音成交(自相关≈0),次日收益不同。持续性+净方向=活跃的知情足迹。区别于裸OFI(净档位变化)与VPIN(分桶|不平衡|):它是主动符号的序列依赖结构。
- **所需字段**：MBO Trade: 主动方side(定s_k=+1主买/-1主卖)、qty、exchtime(排序)。仅连续竞价段成交。
- **计算公式**：逐笔k按exchtime排序:s_k=±1为主动方,q_k=Trade.qty。相邻权重w_k=sqrt(q_k*q_{k-1})(压低孤立小单)。滞后1符号自相关rho1_vw=[Σw_k*s_k*s_{k-1}]/Σw_k∈[-1,1](=2*P(相邻同号)-1,量加权);净符号占比net_sign=Σs_k*q_k/Σq_k∈[-1,1]。因子F1=net_sign*(0.5+0.5*rho1_vw):净方向在持续时被放大、震荡时被压制。
- **聚合到 30min/日频**：全日维护两个标量,O(1)/事件更新,跨3s事件保持exchtime顺序拼接:A(t)=Σ_{exchtime_k<=t}w_k*s_k*s_{k-1};W(t)=Σw_k;S(t)=Σs_k*q_k;Q(t)=Σq_k。t时刻rho1_vw=A/W,net_sign=S/Q。截至t累积,严格无未来。
- **横截面标准化 / 中性化**：rho1_vw、net_sign均已量纲无关且有界;对F1在每个时点做全A股横截面z-score(或rank-to-normal),±3σ winsorize。可另出纯持续性rho1_vw作为交互/条件变量。
- **预期方向**：F1>0(持续净主买)→次日正收益。
- **新颖性**：非裸OFI非VPIN,而是主动符号的序列依赖(拆单代理),量加权相邻同号乘积是基线库没有的生命周期式重构。
- **A股陷阱 / look-ahead**：T+1使主卖持续性(存量抛压)比主买噪音大→可对主买流单独算rho1;涨跌停封板处主动符号退化(全在天花板成交,rho1虚高),剔除last_price触及±10%/5%/20%band(相对昨收)的事件;集合竞价段无连续成交,不纳入。无未来:仅exchtime<=t,前向累积,不用任何mid故无快照对齐问题。

### 20. 对手撤单条件化的毒性净成交额(逆选择加权主动流) `Opponent-withdrawal-conditioned toxic net turnover` ★推荐
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：真正有毒(知情)的主动成交=既激进又对手方拒绝补单——挂单方担心被逆选择而撤价不补。基线'大单净流入'只按成交额切阈值;这里给每笔主动成交按(a)扫穿档数和(b)同一3s内对手方近盘口撤单强度加权。扫单前的撤离=市场相信taker是知情的。
- **所需字段**：MBO Trade(bid_id,ask_id,主动方side,price,qty,exchtime);MBO Cancel(side,price,qty);MBO Order(side,price,qty);快照ap[0..2]/bp[0..2](前一事件,定近盘口带)与turnover。
- **计算公式**：按主动方委托号聚簇(主买用bid_id/主卖用ask_id),簇c可跨3s(按id持续):turnover_c=Σprice*qty;depth_c=c内不同成交价数;sign_c=±1。事件e内对手撤离:主买扫单看ask侧,近盘口撤量CQ_e=Σ Cancel.qty(对手侧,price<=前ap[2]),对手新挂RQ_e=Σ Order.qty(对手侧,price<=前ap[2]);tox_e=CQ_e/(CQ_e+RQ_e+eps)∈[0,1]。簇贡献=sign_c*turnover_c*depth_c*(0.5+tox_e)。
- **聚合到 30min/日频**：逐事件累积TOX(t)=Σ_{最后成交exchtime<=t}sign_c*turnover_c*depth_c*(0.5+tox_e);用快照累计成交额归一:F2(t)=TOX(t)/turnover(t)。得量纲无关的'毒性净参与度'。
- **横截面标准化 / 中性化**：F2为比值(毒性净额/总额);每时点横截面z-score;因depth_c放大幅度,z前先winsorize。
- **预期方向**：F2>0(ask侧不愿补的毒性净主买)→次日正收益。
- **新颖性**：融合基线从不组合的三者:委托号生命周期聚簇、扫穿档数、对手撤单vs补单条件化。是把'大单净'几何化(depth)且生命周期条件化(逆选择权重),非成交额阈值。
- **A股陷阱 / look-ahead**：bid_id/ask_id匹配须处理部分成交与跨3s簇→按id聚合、在t处收口;近盘口带用前一事件快照(勿用扫后被打穿的书);T+1使卖侧毒性部分机械(套牢盘)→买腿更干净;封板剔除;若对手侧id缺失,退化用price对前快照ap/bp档位判扫穿。无未来:tox_e用与扫单同一3s或紧邻前一事件(均exchtime<=t),参考价用前快照。

### 21. 符号非对称Kyle-λ(方向性冲击几何) `Sign-asymmetric Kyle-lambda` ★推荐
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：裸Kyle-λ混合双向且受离群主导。按发起符号拆开冲击弹性:λ_buy=单位主买流引起的mid变动,λ_sell同理。不对称揭示方向性脆弱/需求。若买把价格推得远而卖难压(买贵、book抗卖)→活跃需求且供给稀缺→续涨。用中位数比值(Theil-Sen)稳健化,单笔巨单不主导。
- **所需字段**：快照bp[0],ap[0](mid,前+现事件);MBO Trade(主动方side,qty);AV_ref来自快照volume历史。
- **计算公式**：每3s事件i:mid_i=(bp[0]+ap[0])/2;dret_i=(mid_i-mid_{i-1})/mid_{i-1}。带符号主动量SV_i=Σ_{i内成交}s*qty;归一sv_i=SV_i/AV_ref(AV_ref=该股近~20日每事件成交量中位数,无历史用日内扩窗)。单事件冲击比kappa_i=dret_i/sv_i(仅|sv_i|>=sv_min)。买主导事件sv_i>0,卖主导sv_i<0。
- **聚合到 30min/日频**：lambda_buy(t)=median{kappa_i:i<=t,sv_i>0};lambda_sell(t)=median{kappa_i:i<=t,sv_i<0}(P2/分位sketch滚动)。F3(t)=(lambda_buy-lambda_sell)/(lambda_buy+lambda_sell+eps)∈[-1,1]。
- **横截面标准化 / 中性化**：F3已归一;每时点横截面z。保护:要求买/卖事件各>=K(如15)否则置NaN并向横截面均值收缩(清晨/低流动缺样本)。
- **预期方向**：F3>0(买比卖更推价、抗卖)→次日正收益。
- **新颖性**：把Kyle-λ按发起符号几何化,并用中位数比值(而非OLS)稳健化→方向性、抗离群的流动性不对称信号。基线仅有'裸Kyle-λ'。
- **A股陷阱 / look-ahead**：需足够事件→09:30时点与小盘噪音大;10档mid在低流动股偏宽偏旧→放宽sv_min;封板dret饱和/为零而流巨→剔除封板事件;T+1融券禁使卖侧λ是存量供给弹性而非空头,据此解读。无未来:mid_i、mid_{i-1}均exchtime<=t,滚动中位数只在i<=t。

### 22. 主动流与已实现收益背离(潜伏吸筹/未兑现涨幅) `Aggressive-flow vs realized-return divergence` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：潜伏吸筹:持续净主买被吸收却几乎没推价→信息尚未计入价格→次日兑现。反之净买已伴随大涨则已price-in无边际。把累计带符号流对同期已实现收益残差化,分离未标价的压力。区别于动量(即已实现的价格移动)与OFI(从不比较流与价格结果)。
- **所需字段**：MBO Trade(主动方side,price,qty,exchtime);快照turnover、bp[0]/ap[0](mid)、首事件mid(开盘)/昨收。
- **计算公式**：累计带符号主动成交额STO(t)=Σ_{exchtime<=t}s*price*qty;净主动占比net_aggr_ratio=STO/turnover∈[-1,1]。已实现收益ret(t)=(mid_t-mid_open)/mid_open(mid_open=连续竞价首个mid或昨收以跨日可比)。
- **聚合到 30min/日频**：两分量均可平凡累积到t。每时点横截面z:za=z(net_aggr_ratio),zr=z(ret)。F4(t)=za-zr(买压排名超出价格涨幅排名的部分),可再z(F4)。
- **横截面标准化 / 中性化**：内建:两个横截面z相减即可比;结果再z一次得干净单位信号。
- **预期方向**：F4>0(净买多于价涨→欠反应吸筹)→次日正收益。
- **新颖性**：把订单流对同期已实现收益残差化→捕捉'廉价吸筹'vs'透支追高'。OFI/动量/VWAP偏离都不做此比较;是显式的欠反应构造。
- **A股陷阱 / look-ahead**：涨停是危险退化:ret封顶+10%而流继续堆→F4虚高;剔除封板股(bp[0]巨量、ap侧空)或对ret封顶并打标;极低流动mid_open噪音→用前几个事件mid中位数;T+1使卖驱动F4<0部分机械。无未来:全部exchtime<=t,mid_t取<=t事件快照,开盘取首事件。

### 23. 主动成交规模集中度不对称(买卖成交HHI/熵) `Aggressive trade-size concentration asymmetry` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：成交规模分布形状区分机构足迹与散户噪音。知情机构需求要么表现为少数主导大单(高Herfindahl),要么表现为许多规整均匀的子片(低规模方差,算法冰山)——都不同于异质散户。规模分布更集中/更规整的那一侧是知情侧,方向性集中不对称预示续势。
- **所需字段**：MBO Trade(主动方side,qty)。
- **计算公式**：每侧累计逐笔qty。买:份额p_j=q_j/Σq,HHI_buy=Σp_j^2,熵H_buy=-Σp_j ln p_j,均匀度E_buy=H_buy/ln(n_buy),前1%大单占比TOP_buy=Σ(最大1%买量)/Σ买量;卖侧同理。CONC_ASYM=HHI_buy-HHI_sell;TOP_ASYM=TOP_buy-TOP_sell。
- **聚合到 30min/日频**：每侧累积Σq、Σq^2(得HHI=Q2/Q1^2)与滚动top-k(小顶堆/分位sketch)。F5(t)=0.5*z(CONC_ASYM)+0.5*z(TOP_ASYM)。另出纹理:净主买侧高n+高均匀度+低规模CV=潜伏算法拆单标记。
- **横截面标准化 / 中性化**：HHI依赖笔数→要求每侧最少笔数(如n>=30)否则向0收缩;再对CONC_ASYM、TOP_ASYM各自横截面z后加权。
- **预期方向**：F5>0(买侧规模分布比卖侧更集中/尾更重)→次日正收益。
- **新颖性**：用主动成交量分布的形状(Herfindahl/熵/尾部)做方向性不对称,而非固定成交额阈值。基线'大单净'是阈值化净额;这是自适应分布几何,能捕捉阈值漏掉的均匀切片算法。
- **A股陷阱 / look-ahead**：笔数少时不稳(早时点、小盘)→最小笔数+收缩;A股100股整手与tick量化使极薄股HHI有偏;T+1大卖单可能是机械减持而非信息→买减卖不对称部分缓解;部分交易所冰山隐藏真实规模→若类型字段标冰山,用委托号还原子单规模。无未来:仅exchtime<=t逐笔,滚动矩与top-k只看过去。

### 24. 带符号扫穿档深(每单市价单吃穿档数的紧迫度) `Signed sweep-depth urgency` ★推荐
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：信息半衰期短或高信念的知情者会扫book——一张市价单吃穿多档而不挂单。单张主动单消耗的档位数是干净的紧迫/信念代理,与规模正交。带符号净扫穿档深=知情者的方向性急迫。
- **所需字段**：MBO Trade(bid_id,ask_id,主动方side,price,qty,exchtime);前一事件快照ap[0..9]/bp[0..9]做档位映射。
- **计算公式**：按主动方委托号聚簇(bid_id主买/ask_id主卖),簇c按id跨3s持续。depth_c=c内不同成交价数,等价于对买单:用前事件快照阶梯ap[0..9]数满足ap[l]_prev<=max(c内price)的档数(卖用bp[0..9])。qty_c=Σqty;sign_c。
- **聚合到 30min/日频**：累积NSS(t)=Σ_{在t前收口的簇}sign_c*depth_c*qty_c;按累计主动量归一F6(t)=NSS/Σqty_c。另出深扫占比:depth_c>=2的簇主动量占比(带符号)。
- **横截面标准化 / 中性化**：F6为量加权带符号平均档深(量纲无关);横截面z;深扫重尾,z前winsorize。
- **预期方向**：F6>0(买侧比卖侧吃book更狠)→次日正收益。
- **新颖性**：纯生命周期几何:从委托号聚簇+前书阶梯还原每张市价单的扫穿档深,分离急迫度,与幅度(F2/F5)和净不平衡(OFI)正交。基线无。
- **A股陷阱 / look-ahead**：簇跨3s边界→按id聚合、t处收口;档位映射用前快照阶梯(勿用被打穿的书);集合竞价无扫单;涨停把成交压到单一价→档深退化为1,剔除封板;若无bid_id/ask_id,近似用连续同侧同微秒成交的不同价数(较弱)。无未来:成交exchtime<=t且用前快照阶梯;t时未收口簇按已成交部分贡献。

### 25. 可观测知情成交占比(PIN式交集,因果版) `Observable informed-trade share` ○同族备选
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：用可观测生命周期条件而非MLE/分桶重构PIN的'知情成交量'。当一个簇同时:大(自适应分位)、激进(扫穿>=2档)、且吃到真实挂单(对手方在被打前撤离轻,即taker吃了在场流动性)时标为'知情'。净知情占比(知情买-知情卖成交额/总额)是可直接解读的毒性率。
- **所需字段**：MBO Trade(bid_id,ask_id,主动方side,price,qty,exchtime);MBO Cancel/Order(side,price,qty);快照turnover、前ap[0..2]/bp[0..2]。
- **计算公式**：复用主动簇c。因果规模阈Q90(t)=在c之前收口簇的成交额90分位(日内扩窗或前日)。informed_c=1若[turnover_c>=Q90(t)]且[depth_c>=2]且[同事件对手近盘口撤离轻:CQ_e/(CQ_e+RQ_e+eps)<=0.5即吃到在场流动性];sign_c同前。
- **聚合到 30min/日频**：INFORMED_NET(t)=Σ_{t前收口}informed_c*sign_c*turnover_c;F7(t)=INFORMED_NET/turnover(t)∈[-1,1]。另出总知情占比|INFORMED|/turnover作独立毒性水平(条件变量)。
- **横截面标准化 / 中性化**：比值∈[-1,1];横截面z。因是占比,构造上跨市值可比;仍z控尾。
- **预期方向**：F7>0(净知情买主导)→次日正收益。
- **新颖性**：完全用可观测生命周期交集(大×扫穿×吃到真实流动性)重构PIN/VPIN类量,无MLE、无成交量分桶不平衡。基线仅'裸PIN/VPIN';这是机制新且无未来的重构。
- **A股陷阱 / look-ahead**：相关性:在扫单/大单轴上与F2(幅度)、F6(档深)重叠→对F2、F6正交化(残差)或上线只取其一;Q90须因果(扩窗);T+1使知情卖可能是被迫去杠杆非信息→净(买减卖)框架缓解;封板/集合竞价剔除。无未来:Q90只用c之前簇,对手撤单用同/前事件,簇在t收口,完全因果。

### 26. 主动流的量加权区间位置(订单流的带符号收盘位置值) `Volume-weighted range position of aggressive flow` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：主动成交量在当日价格区间的什么位置?主动买集中在日内高位=信念追涨(强势)而非抄底→续势;大买量在低位或卖在高位=派发/吸收。是应用于订单流的带符号量加权收盘位置值(CLV)——比动量更丰富,因用主动量在区间内的分布而非仅最后价。
- **所需字段**：MBO Trade(主动方side,price,qty,exchtime);快照/成交last_price构建滚动H,L。
- **计算公式**：截至t的日内滚动区间:H(t)=max last_price,L(t)=min(均因果)。逐笔j:pos_j=(price_j-L(t))/(H(t)-L(t))∈[0,1](用截至t区间,输入均<=t无未来)。带符号贡献=s_j*q_j*(pos_j-0.5)。
- **聚合到 30min/日频**：累积NRP(t)=Σ_{exchtime<=t}s_j*q_j*(pos_j-0.5);归一F8(t)=NRP/Σq_j∈[-0.5,0.5](等价VWRP_buy-VWRP_sell,VWRP_side=该侧量加权平均pos)。区间扩张时增量更新pos。
- **横截面标准化 / 中性化**：pos∈[0,1]量纲无关;F8有界;每时点横截面z。区间过小(薄股)时保护分母:要求(H-L)/mid>0.3%否则向0收缩。
- **预期方向**：F8>0(主动买居区间高位、卖居低位=强势持有)→次日正收益。
- **新颖性**：主动订单流的带符号量加权收盘位置值——区别于价格动量(用区间内流的分布)与VWAP偏离(无符号、非区间归一)。捕捉'强势vs派发'。
- **A股陷阱 / look-ahead**：H≈L退化(低流动/低波日)→最小区间保护+收缩;涨停把H钉在band、所有买pos≈1→F8虚高:剔除/封顶封板股;跳空开盘用日内区间(不含昨收)保持pos意义;T+1使卖是持有者故'卖在低位'为真实割肉信息。无未来:H(t)、L(t)仅由exchtime<=t的last_price/成交价构建,对早期成交用截至t区间仍仅用<=t信息。

---

## 视角四、队列弹性 / 隐藏流动性

*重建队列 + 每档笔数，刻画冲击后深度恢复、快照盲区隐藏流动性、补单弹性。*

> 状态：已对抗式评审精选 · 本视角 7 个因子

### 27. 冲击后深度恢复不对称 DRA `Depth Recovery Asymmetry`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：大额主动卖吃穿买盘后,若后续数个3s买盘前3档深度快速回补(强弹性),说明存在坚定承接/隐藏买方,预示支撑与上涨;对主动买冲击后卖盘回补做对称度量,取买弹性-卖弹性。基线OBI/深度只看静态一档水平,DRA捕捉'被打掉后回不回得来'的二阶事件条件化弹性。
- **所需字段**：Trade.qty/Trade.aggr_side(主动方买/卖); 快照 bv[level]/av[level]/exchtime; 用上一事件末快照 e-1 作冲击前状态。
- **计算公式**：DRA_t = mean(R_bid | 买冲击, e+K<=t) - mean(R_ask | 卖冲击, e+K<=t)
- **聚合到 30min/日频**：公共约定: 事件e为3s截面, 仅连续竞价(09:30-11:30/13:00-t)累积, 剔除触及涨跌停/停牌单边报价样本, 深度用股数比值自归一。步1 前3档买深 D_bid_e=Σ_{l=0..2}bv_e[l], 卖深 D_ask_e=Σ_{l=0..2}av_e[l]。步2 冲击识别: 本3s主动卖股数 ST_e=Σ Trade.qty[aggr=卖]; 若 ST_e>=0.3*D_bid_{e-1} 且 D_bid_{e-1}>=股数下限(如100*20)记一次买盘冲击; drop=max(D_bid_{e-1}-D_bid_e, 0.1*D_bid_{e-1})(下限防小分母放大)。步3 恢复(窗口K=5即15s, 仅当 e+K 事件 exchtime<=t 才纳入, 否则该冲击不计入t): R_bid_e=clip((D_bid_{e+K}-D_bid_e)/drop,0,2); 主动买冲击(BT_e>=0.3*D_ask_{e-1})用 D_ask 得 R_ask_e。步4 截至t聚合: DRA_t=mean_{买冲击 e:e+K<=t}R_bid_e - mean_{卖冲击 e:e+K<=t}R_ask_e; 任一侧冲击样本<3记NaN。
- **横截面标准化 / 中性化**：当日全A横截面对 DRA_t 做 winsorize(1/99%)后减截面均值再z-score; 按市值/行业分组中性化去除小盘天然薄弱偏差。
- **预期方向**：正(+): 买盘弹性强于卖盘 -> 隐性承接强 -> 预测次日正收益。
- **新颖性**：把'冲击-回补'做成事件条件化的动态弹性并买卖差分,捕捉基线缺失的二阶自修复信息。评审: 修复drop下限与K窗look-ahead门控(e+K<=t),保留;与REL互补(DRA看尾部大冲击,REL看全分布平均敏感度)。
- **A股陷阱 / look-ahead**：K窗须整体 exchtime<=t 否则未来快照泄漏; 涨跌停/停牌单边簿造成假drop需剔除; drop与D_bid_{e-1}均设下限防除零/放大; 仅用前3档避免1档瞬时清空致除零; T+1使卖压多为存量,买盘弹性更具信息量; 一律用prev-event快照防同窗look-ahead。

### 28. 3秒内闪现流动性不平衡 FLI `Flash Liquidity Imbalance (snapshot-invisible)`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：A股簿全显示,但'挂出即在同一3s内被成交或撤回、从不进任一快照'的挂单是快照视角下真正的隐藏流动性。买侧闪现承接量(被成交部分)占优=有人用不上簿方式默默吸筹,预示上涨;闪现以撤单为主则降权(疑似幌骗)。
- **所需字段**：Order(order_id,side,price,qty); Cancel(order_id,side,price,qty); Trade(qty,aggr_side,bid_id,ask_id)。
- **计算公式**：FLI_t = (FBExec_t - FSExec_t)/(FBExec_t + FSExec_t + ε)
- **聚合到 30min/日频**：步1 生命周期匹配(仅用逐笔即可判定, 无需依赖快照深度): 对每个在本3s窗口内新增的 Order(order_id), 若其 order_id 在同一窗口内又出现在 Trade 的 bid_id/ask_id(被成交)或 Cancel(被撤), 即该单本窗内闭合、跨不到任何快照, 记为闪现单; 跨窗存续单不算。步2 闪现量分性质: FlashBuyExec_e=Σqty(买侧闪现单被成交部分), FlashBuyCancel_e=Σqty(被撤部分); 卖侧对称。步3 截至t累计: FBExec_t=Σ_{e<=t}FlashBuyExec_e, FSExec_t=Σ_{e<=t}FlashSellExec_e。步4 FLI_t=(FBExec_t-FSExec_t)/(FBExec_t+FSExec_t+ε); 辅助 FlashFillRatio_t=闪现被成交/(闪现被成交+闪现被撤) 区分真承接vs幌骗。仅连续竞价累积。
- **横截面标准化 / 中性化**：FLI_t 已在[-1,1]自归一, 横截面减均值后z-score; 闪现被成交总量占全日成交量比作质量权重(小则降权)。
- **预期方向**：正(+): 买侧闪现被成交占优 -> 隐性买方吸筹 -> 正收益; FillRatio低(撤单为主)时降权。
- **新颖性**：利用MBO独占的'快照间不可见'时间尺度定义隐藏流动性, 并拆成执行型(真承接)vs撤销型(幌骗)两性质, 远超裸撤单比。评审: 把'从未进快照'重定义为'同窗闭合'使可落地, 保留。
- **A股陷阱 / look-ahead**：闪现判定=同窗新增且同窗闭合(成交或撤), 无需'从未进快照'的深度反推, 逻辑更硬; A股无冰山单, 机制立足于3s采样盲区; 09:20-09:25不可撤, 只在连续竞价统计; 主动方side从Trade读取; 深/上交所委托号规则差异致匹配失败样本丢弃而非填零。

### 29. 逐档单笔挂量形状与前排集中度 GDS `Granularity Depth Shape`
**novelty 4/5 · lowfreq 5/5 · computable True**

- **信息含义 / 机制**：每档平均单笔挂量=bv[l]/bid_cnt[l]; 少数大单集中前排=深口袋机构守价承接, 多数散小单=散户跟风。买侧'少而大'相对卖侧越突出, 支撑越硬。
- **所需字段**：快照 bv[level]/av[level]/bid_cnt[level]/ask_cnt[level](10档), last_price/exchtime。
- **计算公式**：GDS_t = log((CB_t+ε)/(CA_t+ε)) + (BigBidShare_t - BigAskShare_t)
- **聚合到 30min/日频**：步1 单笔均量 q_bid_e[l]=bv_e[l]/max(bid_cnt_e[l],1), q_ask_e[l]=av_e[l]/max(ask_cnt_e[l],1), 空档(cnt=0)跳过。步2 前排集中度 CB_e=q_bid_e[0]/(mean_{l=1..9,cnt>0}q_bid_e[l]+ε); CA_e 对称。步3 大单占比: 买侧前5档中单笔均量>该股'截至t滚动中位单笔量'2倍的档位,其bv之和/前5档bv之和=BigBidShare_e; 卖侧 BigAskShare_e。步4 截至t按快照数等权取均值得 CB_t,CA_t,BigBidShare_t,BigAskShare_t; GDS_t=log((CB_t+ε)/(CA_t+ε))+(BigBidShare_t-BigAskShare_t)。仅连续竞价; 滚动中位单笔量只用 exchtime<=t 的历史快照估计。
- **横截面标准化 / 中性化**：log比值天然可比; 横截面减均值后z-score; 按股价分档中性化(高价股tick相对小)。
- **预期方向**：正(+): 买侧前排单笔更大/更集中 -> 机构守价支撑 -> 正收益(若市场informed偏好拆小单, 验证集允许符号翻转)。
- **新颖性**：逐档 bid_cnt/ask_cnt 几无人用; 把'笔数与挂量背离'几何化为形状梯度+大单占比+买卖差, 信息独立于总量深度与OBI。评审: 明确大单阈用滚动中位、修log除零, 保留。
- **A股陷阱 / look-ahead**：bid_cnt/ask_cnt=0保护除零; 中位单笔量用截至t滚动而非全日(防look-ahead); 涨跌停封单巨量污染需剔除封板样本; 只用买卖比值与相对占比消量纲。

### 30. 队列前排vs后排消耗-补充速度差 QVA `Queue Velocity Asymmetry`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：用order_id重建队列位置: 消耗从前排(早到单)发生, 补充进后排。买侧前排被快速吃且后排持续补(前消后补快于卖侧)=弹性承接强, 支撑硬。
- **所需字段**：Order(order_id,side,price,qty); Trade(qty,bid_id,ask_id); 快照 bp[level]/ap[level](prev e-1)确定价位与队列基线。
- **计算公式**：QVA_t = log((QVbid_t + ε)/(QVask_t + ε))
- **聚合到 30min/日频**：步1 队列重建: 同一价位按 order_id 递增即时间序, 前排=e-1时点已在该价位队列中的早序单集合。步2 前排消耗 FrontConsumeBid_e=Σ Trade.qty[买单被成交且其 bid_id∈e-1已存在的买单]; 后排补充 BackAddBid_e=Σ Order.qty[买, 本窗新增于最优或次优买价]。步3 截至t分子分母各自累计: QVbid_t=Σ_{e<=t}BackAddBid_e/max(Σ_{e<=t}FrontConsumeBid_e, 最小股数); QVask_t 对称。QVA_t=log((QVbid_t+ε)/(QVask_t+ε))。仅连续竞价。
- **横截面标准化 / 中性化**：log比值 winsorize 后横截面减均值再z-score; 前排样本过少(FrontConsume累计<阈)记NaN。
- **预期方向**：正(+): 买侧'前消后补'快于卖侧 -> 弹性承接 -> 正收益。
- **新颖性**：真正用 order_id 重建队列位置区分前/后排动态, 基线撤单比与OFI皆不含队列位置; 把弹性拆成消耗端与补充端两速度, 生命周期化重构。评审: 分子分母各自累计防抖, 保留(OEX因与其消耗端重叠且近乎方向化best换手而剔除)。
- **A股陷阱 / look-ahead**：前排判定用 e-1 已存在的单(不含本窗新单)防未来单混入; 分子分母各自全日累计再相除防小分母放大; 需可靠 order_id 单调性与 bid_id/ask_id 匹配, 匹配失败样本丢弃; 稀薄票最小样本阈; 仅连续竞价。

### 31. 补单追价vs守价倾向 RAP `Replenishment Aggressiveness`
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：深度被消耗后新补买单挂在何处: 挂更靠近/高于原最优价(追价)=急切承接需求; 守在原价或更低=被动支撑。追价补单是主动看多信号。
- **所需字段**：Order(price,side,qty); Trade(qty,aggr_side)判冲击; 快照 bp[0]/ap[0](prev e-1); tick=0.01。
- **计算公式**：RAP_t = AggrBuy_t - AggrSell_t (tick)
- **聚合到 30min/日频**：步1 冲击后窗: 对每个买盘冲击事件(同DRA判据), 取其本3s及次3s(次窗须 exchtime<=t)新增买单 Order[买]。步2 追价度(以tick计)=(Order.price-bp_{e-1}[0])/tick, 正=追价高挂, 负=守价低挂。步3 量加权 AggrBuy_e=Σ qty*(Order.price-bp_{e-1}[0])/tick / Σ qty (冲击后窗内全部补买单, 含正负); 卖侧对称 AggrSell_e=Σ qty*(ap_{e-1}[0]-Order.price)/tick / Σ qty(挂低于ap为追价卖)。步4 截至t按事件量加权累计求均值 AggrBuy_t, AggrSell_t; RAP_t=AggrBuy_t-AggrSell_t(tick单位)。仅连续竞价。
- **横截面标准化 / 中性化**：以tick为单位, 但绝对tick数与股价相关, 横截面按股价分档中性化后减均值z-score。
- **预期方向**：正(+): 买方被打后追价补单强于卖方 -> 急切承接 -> 正收益。
- **新颖性**：区分'追价vs守价'补单几何, 对撤补行为方向化; 基线只数撤单不看补单挂在哪, 本因子把补充位置(相对最优价的进攻性)条件在冲击后。评审: 去掉max截断以保留守价负信号、加次窗look-ahead门控与股价中性化, 保留。
- **A股陷阱 / look-ahead**：mid/bp/ap一律用 e-1 prev快照; 次3s窗须 exchtime<=t; 涨跌停附近价位受限(封在停板价)需剔除; tick=0.01, 双创/科创注意价位规则; 保留正负追价度(不再只取max>0)以完整刻画守价vs追价; 仅连续竞价。

### 32. 深度流失的撤单vs成交归因(脆弱度)DFR `Depth Fragility Ratio`
**novelty 4/5 · lowfreq 5/5 · computable True**

- **信息含义 / 机制**：买盘深度减少若主要来自撤单(易变、幌骗、恐慌)而非成交(真实换手), 则支撑虚; 买侧'成交主导流失、低撤单脆弱度'相对卖侧越优, 支撑越实。
- **所需字段**：Trade(qty,aggr_side); Cancel(side,qty)。仅需逐笔, 无需快照(自平衡)。
- **计算公式**：DFR_t = FragAsk_t - FragBid_t
- **聚合到 30min/日频**：步1 每事件同侧深度流失归因: TradeOutBid_e=Σ Trade.qty[aggr=卖](成交消耗买单); CancelOutBid_e=Σ Cancel.qty[side=买]。步2 截至t分子分母各自累计: FragBid_t=Σ_{e<=t}CancelOutBid_e/(Σ_{e<=t}(CancelOutBid_e+TradeOutBid_e)+ε); FragAsk_t 对称(TradeOutAsk=aggr=买的成交, CancelOutAsk=side=卖的撤单)。步3 DFR_t=FragAsk_t-FragBid_t(买侧越不脆弱、卖侧越脆弱则越大)。仅连续竞价。
- **横截面标准化 / 中性化**：比值∈[0,1], 差分∈[-1,1]; 横截面减均值后z-score; 用当日撤+成交总量加权确保稳定。
- **预期方向**：正(+): 买盘稳(成交主导)而卖盘脆(撤单主导, 存量卖压不坚决) -> 支撑强 -> 正收益。
- **新颖性**：把撤单与成交做同侧深度流失的竞争归因并买卖差分, 区分'虚支撑vs实支撑', 是裸撤单比的条件化/几何化重构。评审: 全日累计口径明确、竞价时段处理修正, 保留。
- **A股陷阱 / look-ahead**：主动卖qty对应买单被消耗, 主被动勿混; 分子分母各自全日累计防小样本抖动; 09:20-09:25不可撤会人为压低该段撤单, 只统计连续竞价; T+1下买卖撤单信息量不对称, 差分设计部分吸收。

### 33. 深度对净委托流的回补弹性系数 REL `Resilience Elasticity`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：把弹性做成回归斜率: 单位净卖压带来的买盘前3档深度变化。斜率越接近0甚至为正(卖压来时买深不降反补)说明弹性极强、隐性买方托底。
- **所需字段**：Trade(qty,aggr_side); 快照 bv[level]/av[level](前3档, e与e-1); exchtime。
- **计算公式**：REL_t = β_ask_t - β_bid_t, 其中 ΔD = α + β*NetPress + ε (截至t滚动OLS)
- **聚合到 30min/日频**：步1 每事件净流与深度变化: NetSellPress_e=(ST_e-BT_e)/(ST_e+BT_e+ε)(主动卖/买股数, ∈[-1,1]); ΔDbid_e=(D_bid_e-D_bid_{e-1})/max(D_bid_{e-1},股数下限)(前3档相对变化)。步2 截至t滚动OLS(仅 e<=t 样本, winsorize去极值): ΔDbid=α+β_bid*NetSellPress+noise, 取 β_bid_t(买深对卖压敏感度, 越大/越不为负=越抗压); 对称回归 ΔDask 对 NetBuyPress 得 β_ask_t。步3 REL_t=β_ask_t-β_bid_t。用回归 R^2 作质量权重; 样本<20记NaN。仅连续竞价。
- **横截面标准化 / 中性化**：β 已无量纲; 横截面 winsorize+减均值z-score。
- **预期方向**：正(+): 卖压来袭买盘深度更能维持/回补(高β_bid)且强于卖侧 -> 强弹性隐性承接 -> 正收益。
- **新颖性**：把Kyle-λ思路从'价格对流量'挪到'深度对流量'并做买卖弹性差, 是弹性的回归几何化, 区别于裸Kyle-λ(价冲击)与已实现波动, 度量簿的自我修复能力。评审: 加样本阈/去极值/R^2权重, 保留。
- **A股陷阱 / look-ahead**：回归只用 exchtime<=t 样本, β为截至t因果估计无泄漏; 足够样本+去极值防斜率被单点主导; 涨跌停期间ΔD异常需剔除; NetPress用股数而非笔数; 停牌/瞬时空簿事件跳过; 与DRA互补(REL全分布平均, DRA大冲击尾部)。

---

## 视角五、盘口几何形状

*超越简单 OBI，用衰减弹性/洛伦兹曲率/最优传输/隐藏墙等几何量刻画 10 档形状。*

> 状态：生成候选（未过对抗评审；★=推荐, ○=同族备选） · 本视角 11 个因子

### 34. 对数深度衰减弹性的买卖不对称 `Log-Depth Decay Elasticity Asymmetry (LDDEA)` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：盘口深度随价距衰减的斜率=供给弹性。衰减慢的一侧说明即便偏离最优价仍有厚墙托着(供给富有弹性、被动方坚定)。T+1 下卖侧多为持仓者被动挂单、买侧为真实新增需求; 买侧衰减比卖侧慢意味着回撤有承接、上涨阻力被逐步吃掉,看多。
- **所需字段**：bv[0..9], av[0..9], bp[0..9], ap[0..9], exchtime
- **计算公式**：每 snapshot 对买卖侧分别加权最小二乘(仅有效档): 买侧 ln(bv[l]+1)=α_b−β_b·db[l], 卖侧 ln(av[l]+1)=α_a−β_a·da[l], 其中 db[l]=(bp[0]-bp[l])/tick, da[l]=(ap[l]-ap[0])/tick(用真实tick价距而非档序号以吸收gappy盘口)。β 为衰减弹性(越大衰减越快)。per-snapshot 量 X_s=β_a,s−β_b,s(>0 表示买侧衰减更慢/更厚实弹性)。有效档<3 该 snapshot 置缺失。
- **聚合到 30min/日频**：时点t值 = Σw_s·X_s/Σw_s, 权重 w_s=Δexchtime_s(相邻纳秒差,首笔取3e9), 仅用 exchtime<=t 的连续竞价事件(剔除集合竞价段与首笔连续竞价前), 时间加权均值天然低换手; 8个时点(09:30..14:30)各一值, 建议另出全日版(t=收盘前)。
- **横截面标准化 / 中性化**：因斜率量纲已无量纲(ln-vol 对 tick), 每时点内 winsorize1/99 后对 ln(市值)+行业+当日截至t换手率回归取残差再 z-score。
- **预期方向**：正(X 越大买侧供给越有弹性 → 前向收益越高)
- **新颖性**：基线只做静态 OBI/多档加权和。此处提取深度对价距的一阶弹性(ln-线性斜率)及其买卖不对称, 且以真实 tick 价距为自变量而非档序号, 前人多忽略档间跳空导致斜率被高估。
- **A股陷阱 / look-ahead**：封涨停时卖侧几乎清空、β_a 不可靠→该 snapshot 置缺失; 有效档<3 不拟合; 低价股 tick 相对价大、db 数值偏小已被无量纲与横截面标准化吸收。look-ahead: 仅用 exchtime<=t 的 snapshot, 无未来泄漏。

### 35. 深度集中度(Herfindahl/熵)的买卖不对称 `Depth Concentration Asymmetry (DCA)` ○同族备选
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：深度是集中在少数档(尤其最优档)还是摊到全书, 反映挂单者的紧迫/信念。买侧高度集中在最优档=急切买盘(可能冰山补单), 卖侧摊薄=卖压不急。集中度不对称刻画'谁更急'。
- **所需字段**：bv[0..9], av[0..9], exchtime
- **计算公式**：每 snapshot: 买侧份额 p_l=bv[l]/Σbv, HHI_b=Σp_l^2, 归一熵 H_b=−Σp_l ln p_l/ln(n_b)(n_b=买侧有效档数); 卖侧同理得 HHI_a、H_a。per-snapshot 主量 X_s=HHI_b,s−HHI_a,s(>0 买侧更集中), 可选熵版 Xh_s=H_a,s−H_b,s。n<2 的一侧置缺失。
- **聚合到 30min/日频**：时点t值 = Σw_s·X_s/Σw_s, w_s=Δexchtime_s(首笔3e9), 仅用 exchtime<=t 连续竞价事件(剔集合竞价), 天然低换手; 8时点各一值, 另出全日版。
- **横截面标准化 / 中性化**：HHI/熵已归一到[0,1], 横截面 z-score + 市值/行业/换手中性。
- **预期方向**：正(X>0 买侧集中于最优档 → 短期急切买盘 → 前向收益偏正); 需与因子7单笔挂量联合排除纯挂单急切≠成交。
- **新颖性**：把信息论集中度/熵用到'买卖两侧对比'而非单侧, 且区分 HHI(尾部)与熵(整体形状)两种口径; 基线因子库无深度分布形状量。
- **A股陷阱 / look-ahead**：单侧仅1档有效时 HHI=1、熵=0 为退化, 用 n>=2 才纳入; 停牌/涨跌停单侧退化剔除。look-ahead: 无。

### 36. 累积深度洛伦兹曲率(前置/后置流动性) `Cumulative Depth Lorenz Convexity (CDLC)` ★推荐
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：把'累积深度 vs 累积价距'画成洛伦兹型曲线, 曲率区分流动性是前置(贴近最优价, 曲线凸)还是后置(远离最优价, 曲线凹)。前置买侧流动性=近价强承接; 后置=近价空、需大幅让价才有量, 脆弱。
- **所需字段**：bv[0..9], av[0..9], bp[0..9], ap[0..9], exchtime
- **计算公式**：每 snapshot 买侧: 累积深度 C_b(l)=Σ_{k<=l}bv[k], 归一 y_l=C_b(l)/C_b(last); 累积价距归一 x_l=db[l]/db[last]。凸度 G_b=2·Σ_l(y_l−x_l)·Δx_l(曲线与对角线所围有向面积, >0 凸=前置)。卖侧同理得 G_a。per-snapshot 量 X_s=G_b,s−G_a,s(买侧比卖侧更前置)。有效档<4 置缺失。
- **聚合到 30min/日频**：时点t值 = Σw_s·X_s/Σw_s, w_s=Δexchtime_s(首笔3e9), 仅用 exchtime<=t 连续竞价事件(剔集合竞价), 天然低换手; 8时点各一值, 另出全日版。
- **横截面标准化 / 中性化**：面积量∈约[−1,1], 直接横截面 z-score + 中性化。
- **预期方向**：正(X>0 买侧流动性更贴近最优价 → 近价承接强 → 前向收益偏正)
- **新颖性**：借用洛伦兹/基尼几何度量盘口'流动性前后置', 是对'单位深度价差曲线'的分布形状化; 前人极少把 book 当作洛伦兹曲线处理。
- **A股陷阱 / look-ahead**：单边退化(涨跌停)分布不可比→剔除; 有效档少时曲线离散噪声大, n<4 剔除; 统一到相同价距网格(用 tick 价距而非档序号)。look-ahead: 无。

### 37. 走簿冲击成本曲线的买卖不对称与凸性 `Walk-the-Book Impact Asymmetry & Convexity (WBIAC)` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：把 10 档当作即时冲击成本曲线: 吃掉固定金额 Q 的成交要走多深决定瞬时冲击成本。买入成本(走卖侧)与卖出成本(走买侧)的不对称=方向性流动性压力; 成本凸性(吃2Q vs Q 的边际抬升)=深层脆弱。
- **所需字段**：ap[0..9], av[0..9], bp[0..9], bv[0..9], turnover, volume, exchtime
- **计算公式**：每 snapshot 取标的自身尺度 Q(建议=统一固定名义 20万/100万元两档, 或当日截至t的3s均成交额固定倍数)。买入: 沿 ap[0..9] 逐档累积吃满 Q 得 VWAP_buy, 成本 Cb(Q)=VWAP_buy/mid−1; 卖出沿 bp 得 Cs(Q)。不对称 AI_s=Cb,s(Q)−Cs,s(Q)(>0 买更贵=卖侧薄/买侧承接厚)。凸性 CV_s=Cb(2Q)/Cb(Q) 与卖侧对称版之差。10 档吃不满 Q 用末档价外推并记 flag。per-snapshot 主量 AI_s。
- **聚合到 30min/日频**：时点t值 = Σw_s·AI_s/Σw_s, w_s=Δexchtime_s(首笔3e9), 仅用 exchtime<=t 连续竞价事件(剔集合竞价); CV 作独立子因子同法聚合; 8时点各一值, 另出全日版。
- **横截面标准化 / 中性化**：Cb/Cs 已按 mid 归一; 横截面 z-score + 流动性(ADV)中性——Q 若用固定名义, 小盘成本天然高, 必须做 ADV 中性。
- **预期方向**：正(AI>0 买入相对更贵即买侧承接强于卖侧供给 → 前向收益偏正)
- **新颖性**：非裸 Kyle-lambda: 显式几何化整条冲击成本曲线并做买卖不对称+凸性双口径, 用固定名义/ADV 校准 Q 使横截面可比, T+1 卖压方向被显式建模。
- **A股陷阱 / look-ahead**：封板一侧无量→该侧成本发散, 单边行情剔除或截断并 flag; 深度不足外推需谨慎打 flag; Q 若用固定名义必须 ADV 中性否则退化为流动性代理。look-ahead: 用 <=t 的当日累计成交额算 Q, 无未来。

### 38. 多档微价格期限结构斜率 `Multi-Level Microprice Term-Structure Slope (MMTS)` ○同族备选
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：逐层加入更深档位重估的'微价格'序列, 类似期限结构: 若纳入深档后微价格系统性上移, 说明深层买盘压力强于表层, 是超越 top-of-book 的隐性看多信号。
- **所需字段**：ap[0], bp[0], bv[0..9], av[0..9], exchtime
- **计算公式**：每 snapshot 对 k=1..K(K=非空最小档数): 累积买量 CB_k=Σ_{l<k}bv[l], 累积卖量 CA_k=Σ_{l<k}av[l], 不平衡 I_k=CB_k/(CB_k+CA_k), 第k层微价格 M_k=ap[0]·I_k+bp[0]·(1−I_k)。对序列{(k,M_k)}做斜率回归得 slope_s, 归一 X_s=slope_s/mid; 副量端点偏离 D_s=(M_K−M_1)/mid。K<3 置缺失。
- **聚合到 30min/日频**：时点t值 = Σw_s·X_s/Σw_s, w_s=Δexchtime_s(首笔3e9), 仅用 exchtime<=t 连续竞价事件(剔集合竞价); 端点偏离 D_s 同法聚合作副量; 8时点各一值, 另出全日版。
- **横截面标准化 / 中性化**：已按 mid 归一; 横截面 z-score + 中性化。
- **预期方向**：正(X>0 深档微价格上移 → 深层买压 → 前向收益偏正)
- **新颖性**：微价格本身接近基线, 但'把微价格按纳入档数展开成期限结构并取斜率'是新颖重构, 度量表层 vs 深层压力差, 而非单一微价格-mid 偏离。
- **A股陷阱 / look-ahead**：最优档量为0/极小时 I_k 波动大, top 档退化用 K>=3 起步; 涨跌停单边 I_k 恒0/1 剔除。look-ahead: 无。

### 39. 逐笔委托整数价位聚集的买卖不对称 `Order-Price Round-Number Clustering Asymmetry (OPRCA)` ★推荐
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：散户偏好在整元/整角等心理价位挂单, 机构算法更均匀。买侧整数价位挂单占比高=散户情绪化买盘(常为弱手、逆向), 卖侧整数聚集=散户止盈墙。用逐笔委托(add)价格的网格聚集刻画参与者结构。
- **所需字段**：逐笔委托 Order: side, price, qty, order_id; snapshot bp[0]/ap[0](推 mid 做近价过滤); 可选 Cancel(order_id)做生命周期过滤; exchtime
- **计算公式**：在[开盘,t]累积所有逐笔委托 add 事件(Order: side/price/qty)。整齐度权 r(price): price mod 1.00=0 记 w=2, mod 0.10=0 记 w=1, 否则0。买侧整数聚集比 RC_b=Σ_{buy,r>=1}qty/Σ_{buy}qty(可按 w 加权), 卖侧 RC_a 同理。因子 X(t)=RC_b−RC_a。建议只统计价格在当前 mid±k tick 内的挂单以剔除远端垃圾单。
- **聚合到 30min/日频**：在[开盘,t]直接累积区间内所有委托量求比(天然累积、日频稳定), 撤单匹配也只用 exchtime<=t; 8时点各一值(逐时点更新累积), 另出全日版。
- **横截面标准化 / 中性化**：比值∈[0,1], 横截面 z-score; 按股价区间中性(高价股一元步长稀疏、聚集机制不同)。
- **预期方向**：负(RC_b 偏高=散户情绪化买盘聚集=弱手主导 → 前向收益偏负, 逆向); 方向需实测, 先验 X>0→轻微负。
- **新颖性**：对逐笔委托价格做整数/心理价位网格聚集并按买卖侧拆分, 是参与者结构的行为几何刻画; 现有因子库几乎无 price-clustering 且从不区分挂单方向, 高度正交。
- **A股陷阱 / look-ahead**：需引擎暴露逐笔委托 price/side/qty(用户已确认数据含 Order); 09:20-09:25 不可撤单、集合竞价段语义不同应剔除; 高价股 tick 相对小、整元罕见需按价格分层; 应剔除挂后立即撤的幌骗单(order_id 与 Cancel 匹配, 存活<阈值不计)。look-ahead: 仅累积 exchtime<=t 委托, 撤单匹配也只用<=t。

### 40. 逐档平均单笔挂量的斜率与最优档块度 `Per-Level Avg Order Size Slope & Best-Level Lumpiness (POSSL)` ★推荐
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：bv[l]/bid_cnt[l] 是该档平均单笔挂量。机构大单常压在最优/次优档形成'块', 散户小单摊在全书。最优档单笔挂量相对全书越大=有大玩家坐镇该侧; 买侧块度大→机构买盘。
- **所需字段**：bv[0..9], av[0..9], bid_cnt[0..9], ask_cnt[0..9], bp[0..9], ap[0..9], exchtime
- **计算公式**：每 snapshot 逐档: 买侧单笔挂量 qb[l]=bv[l]/max(bid_cnt[l],1), 卖侧 qa[l]=av[l]/max(ask_cnt[l],1)。(a) 最优档块度 L_b=qb[0]/median(qb[1..]), L_a 同理; (b) 单笔挂量随价距斜率 γ_b=OLS(qb[l]~db[l])。per-snapshot 主量 X_s=ln(L_b,s+1)−ln(L_a,s+1)(买侧最优档更块状); 次量 γ 不对称。
- **聚合到 30min/日频**：时点t值 = Σw_s·X_s/Σw_s, w_s=Δexchtime_s(首笔3e9), 仅用 exchtime<=t 连续竞价事件(剔集合竞价); γ不对称同法作副量; 8时点各一值, 另出全日版。
- **横截面标准化 / 中性化**：横截面 z-score; 因 cnt 精度不同用 ln 稳健化 + 市值/换手中性。
- **预期方向**：正(X>0 买侧最优档大单坐镇 → 机构承接 → 前向收益偏正); 需与撤单率联合排除幌骗。
- **新颖性**：利用常被忽略的 bid_cnt/ask_cnt 笔数字段构造'单笔挂量'的档间几何(斜率+块度), 区分机构块单 vs 散户碎单, 与单纯量的不平衡正交。
- **A股陷阱 / look-ahead**：最优档大单可能是幌骗/冰山, 强烈建议用 MBO(order_id↔Cancel)算该档大单存活时长做真伪加权, 幌骗墙降权甚至反号; bid_cnt/ask_cnt 某交易所/版本可能缺失, 缺失则退化为不可算需 flag。look-ahead: 若用存活时长只用<=t。

### 41. 买卖侧深度形状的有向分布距离 `Signed Book-Shape Wasserstein Divergence (SBSWD)` ★推荐
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：把买、卖两侧归一化深度看成'离 mid 价距'轴上的两个概率分布, 用最优传输距离量化二者形状差异, 并按谁的质量更贴近 mid 赋号。抓的是形状不对称(质心+离散+尾部综合), 而非一阶量的净不平衡。
- **所需字段**：bv[0..9], av[0..9], bp[0..9], ap[0..9], exchtime
- **计算公式**：每 snapshot: 买侧分布 pb_l=bv[l]/Σbv 定义在价距{db[l]}, 卖侧 pa_l=av[l]/Σav 定义在{da[l]}。1-Wasserstein W_s=Σ|F_b(x)−F_a(x)|Δx(F 为累积分布, 同一价距正轴)。符号 sign_s=sgn(COM_a−COM_b), COM=Σp_l·dist_l。有向量 X_s=sign_s·W_s(>0 买侧质量更贴 mid 且形状更集中于近价)。有效档<4 置缺失。
- **聚合到 30min/日频**：时点t值 = Σw_s·X_s/Σw_s, w_s=Δexchtime_s(首笔3e9), 仅用 exchtime<=t 连续竞价事件(剔集合竞价); 8时点各一值, 另出全日版。
- **横截面标准化 / 中性化**：W 以 tick 为单位, 需除以标的近期平均相对价差或横截面 rank 消量纲后 z-score + 中性化。
- **预期方向**：正(X>0 买侧形状更贴近 mid → 前向收益偏正)
- **新颖性**：用最优传输/分布距离度量买卖盘口'形状'差异并有向化, 综合质心、离散度、尾部, 远超标量 OBI 或加权不平衡, 在 A 股因子库中几乎空白。
- **A股陷阱 / look-ahead**：单边退化(涨跌停)分布不可比→剔除; 有效档少时 W 噪声大, n<4 剔除; 必须统一到相同 tick 价距网格(用价距而非档序号)。look-ahead: 无。

### 42. 深度质心/有效盘口宽度的买卖不对称 `Depth Center-of-Mass Width Asymmetry (DCMWA)` ○同族备选
**novelty 3/5 · lowfreq 5/5 · computable True**

- **信息含义 / 机制**：深度质心=该侧流动性平均离最优价多远。买侧质心近(挂单紧贴)+卖侧质心远(卖家不愿贴价)→买家急、卖家惜售→看多。两侧质心之和(有效宽度)大=流动性弥散、盘口脆弱。
- **所需字段**：bv[0..9], av[0..9], bp[0..9], ap[0..9], exchtime
- **计算公式**：每 snapshot: COM_b=Σ_l bv[l]·db[l]/Σ_l bv[l](tick), COM_a=Σ_l av[l]·da[l]/Σ_l av[l]。不对称 X_s=COM_a,s−COM_b,s(>0 卖侧更远、买侧更紧); 副产物有效宽度 EW_s=COM_a,s+COM_b,s(脆弱度)。
- **聚合到 30min/日频**：时点t值 = Σw_s·X_s/Σw_s, w_s=Δexchtime_s(首笔3e9), 仅用 exchtime<=t 连续竞价事件(剔集合竞价); EW 副量同法聚合作独立脆弱度因子; 8时点各一值, 另出全日版。
- **横截面标准化 / 中性化**：以 tick 为单位, 横截面按股价/tick 结构分层后 z-score; EW 单独作脆弱度因子(方向负)。
- **预期方向**：正(X>0 买紧卖远 → 前向收益偏正); 副量 EW 大 → 前向收益偏负
- **新颖性**：简单但非 OBI: 用一阶矩(质心/宽度)而非零阶(总量比)刻画盘口, 且拆成方向不对称与整体弥散两个正交子量。
- **A股陷阱 / look-ahead**：涨跌停单边质心退化剔除; 低价股 tick 距离粗, 需按股价/tick 结构分层。look-ahead: 无。

### 43. 盘口 tick 跳空缺口度的买卖不对称 `Book Tick-Gap Sparsity Asymmetry (BTGSA)` ○同族备选
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：10 档之间若存在空 tick(相邻档价差>1 tick), 说明该侧挂单稀疏、价格可被轻易推动。卖侧多缺口→上推阻力小; 买侧多缺口→下方无承接易踩踏。缺口不对称=方向性易推动度。
- **所需字段**：bp[0..9], ap[0..9], exchtime
- **计算公式**：每 snapshot 买侧缺口 GAP_b=Σ_{l=0}^{8}[(bp[l]−bp[l+1])/tick−1](被跳过的空 tick 数), 卖侧 GAP_a=Σ[(ap[l+1]−ap[l])/tick−1]。per-snapshot 量 X_s=GAP_b,s−GAP_a,s(>0 买侧更稀疏/下方更空)。副量占用度 OCC=(买侧有效档+卖侧有效档)/20。
- **聚合到 30min/日频**：时点t值 = Σw_s·X_s/Σw_s, w_s=Δexchtime_s(首笔3e9), 仅用 exchtime<=t 连续竞价事件(剔集合竞价); 亦可出'有缺口 snapshot 占比'; 8时点各一值, 另出全日版。
- **横截面标准化 / 中性化**：按股价/tick 分层后 z-score(高价股天然多缺口)+ 中性化。
- **预期方向**：负(X>0 买侧更空/下方无承接 → 前向收益偏负; X<0 卖侧空、上方易推 → 偏正)
- **新颖性**：显式统计 10 档内 tick 级跳空缺口并做买卖不对称, 直接度量'价格可推动性/盘口脆弱', 前人多只看深度不看价格网格的稀疏结构。
- **A股陷阱 / look-ahead**：高价股(如百元股)tick 相对价小、档间天然多空 tick→必须按股价/tick 分层或用相对缺口((bp[l]−bp[l+1])/mid); 涨跌停附近排队密集缺口消失属正常。look-ahead: 无。

### 44. 深度单调性破坏(隐藏墙)有向强度 `Depth Monotonicity-Violation Hidden-Wall Signal (DMVHW)` ★推荐
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：正常盘口深度随价距递减; 若某深档量显著大于其上各档(bv[l+1]≫bv[l]), 是刻意停放的'墙'——买侧墙=支撑, 卖侧墙=压力。墙的位置与相对强度提供方向信号且相对静态(低换手)。
- **所需字段**：bv[0..9], av[0..9], bp[0..9], ap[0..9], exchtime; 强化用 Order/Cancel/Trade(order_id, bid_id, ask_id)做墙的真伪过滤
- **计算公式**：每 snapshot 买侧: 找最大墙比 WB_b=max_{l>=1}(bv[l]/max(bv[0..l-1])), 记其价距 dpos_b; 卖侧 WB_a、dpos_a 同理。有向强度 X_s=f(WB_b,dpos_b)−f(WB_a,dpos_a), 其中 f=(WB−1)_+·exp(−dpos/τ)(墙越强、越贴近最优价权重越大, τ=3 tick)。>0 买侧支撑墙强于卖侧压力墙。
- **聚合到 30min/日频**：时点t值 = Σw_s·X_s/Σw_s, w_s=Δexchtime_s(首笔3e9), 仅用 exchtime<=t 连续竞价事件(剔集合竞价); 亦可出'买侧存在有效墙(WB_b>1.5)的 snapshot 占比'; 8时点各一值, 另出全日版。
- **横截面标准化 / 中性化**：横截面 z-score(f 已无量纲)+ 市值/换手中性。
- **预期方向**：正(X>0 下方支撑墙占优 → 前向收益偏正); 需过滤幌骗墙, 见 pitfalls。
- **新颖性**：把深度序列的'非单调反常'(inversion)显式建模为隐藏墙并有向、按距离衰减加权, 是对 book 形状的'反常检测'新颖用法, 与所有平滑型形状因子正交。
- **A股陷阱 / look-ahead**：墙极可能是幌骗单: 强烈建议用 MBO(order_id↔Cancel)算该档大单存活时长/成交前撤单率对墙做真伪加权, 幌骗墙降权甚至反号; 涨跌停封单本身是巨墙需单独处理(封板墙不算 alpha)。look-ahead: 若用生命周期过滤只用 exchtime<=t 的 Order/Cancel/Trade 匹配, 无未来。

---

## 视角六、A股制度特有微结构

*只有理解 A 股制度（涨跌停/竞价/T+1/tick）才想得到的因子。*

> 状态：已对抗式评审精选 · 本视角 10 个因子

### 45. 封板脆弱指数 `Limit-Up Seal Fragility via order lifecycle`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：When sealed at limit-up the naive read is thick queue=bullish. The informative signal is churn inside the frozen wall: queued buy volume repeatedly cancelled vs committed. High cancel/add at the pinned price = weak hands = board likelier to open / not gap up. Reconstructable only via order_id/BidId lifecycle at a single frozen price.
- **所需字段**：last_price, bp[0], bv[0], MBO Order.side/price/qty/order_id, MBO Cancel.side/price/qty/order_id, MBO Trade.Price/Qty/Side/BidId, exchtime, prev_close(split/div adjusted)
- **计算公式**：Adjusted p*=round(prev_close_adj*(1+cap),2), cap in {0.05 ST,0.10 main,0.20 STAR/ChiNext}. AT_LIMIT_UP flag when last_price==p* (integer-cent compare). Restrict MBO with exchtime<=t (time-of-day ns) to buy side at price==p*: AddQ=sum Order.qty(side=buy,price==p*); CxlQ=sum Cancel.qty(side=buy,price==p*); ExecQ=sum Trade.Qty(Price==p*, aggressor=sell). Running day-to-date sums from first seal event t0: A=sum AddQ, C=sum CxlQ, E=sum ExecQ. Fragility(t)=C/(A+1e-9); Fragility2(t)=C/(bv[0]+1e-9) using seal thickness S_t=bv[0] at AT_LIMIT_UP snapshots. NaN if never sealed. Daily value = last valid AT_LIMIT_UP timepoint value (also emit intraday-max variant). Continuous session only (no 09:20-09:25 cancels).
- **聚合到 30min/日频**：Per 3s increment A,C,E from that window's MBO; emit Fragility at each AT_LIMIT_UP timepoint; daily = last valid or intraday-max.
- **横截面标准化 / 中性化**：Universe = only names sealed at limit-up at that timepoint; winsorize 1/99 then rank-normal WITHIN the sealed group (do NOT z-score against non-sealed names); bucket by board.
- **预期方向**：Negative: higher cancel-to-add churn inside the seal -> weaker hands -> lower next-day return.
- **新颖性**：Reads the cancel-vs-add intention distribution behind the wall at the pinned price, invisible to snapshot-only or aggregate-flow factors; requires order_id matching.
- **A股陷阱 / look-ahead**：prev_close must be split/dividend adjusted or limit detection fails. Sealed snapshot ap/av can be stale, rely on MBO. No cancels 09:20-09:25. Only MBO with exchtime<=t (no look-ahead).

### 46. 开板振荡强度与二次封板资金比 `Board-opening oscillation & re-seal capital ratio`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：A stock can seal, open, and re-seal repeatedly, each open a conviction test. Many opens = weak; a strong re-seal (fresh large buy limits re-absorbing released supply) = conviction. Encodes the intraday limit battle that snapshots flatten.
- **所需字段**：last_price, ap[0], av[0], MBO Order.side/price/qty, MBO Trade.Price/Qty/Side, exchtime, prev_close(adjusted)
- **计算公式**：p* as in factor 1. State machine on events with exchtime<=t: SEALED when last_price==p* and no executable ask below p*; OPEN when previously SEALED and (ap[0]==p* with av[0]>0 persisting >=1 further event) OR a real Trade.Price<p* prints. NumOpens(t)=count SEALED->OPEN transitions. Within each OPEN interval: ReleasedSell=sum Trade.Qty(Price==p*,aggressor=sell); ReSealCap=sum Order.qty(side=buy,price==p*) that restores SEALED. OpenCount(t)=NumOpens; ReSealRatio(t)=sum ReSealCap/(sum ReleasedSell+1e-9); OpenDurationFrac(t)=seconds-in-OPEN / seconds-since-first-seal. Daily = end-of-continuous-session values.
- **聚合到 30min/日频**：Per 3s update state machine + counters; emit running values; daily EOD.
- **横截面标准化 / 中性化**：Conditional on ever-sealed universe; rank-normal within board group.
- **预期方向**：OpenCount, OpenDurationFrac negative; ReSealRatio positive (strong re-absorption = conviction).
- **新颖性**：Discrete seal/open/re-seal state machine with released-vs-re-sealing volume attribution, an A-share limit-mechanism construct with no continuous-market analog.
- **A股陷阱 / look-ahead**：Distinguish genuine open from one-tick print noise (require av[0] persistence or a real sub-p* Trade). Adjusted prev_close mandatory. Build a separate symmetric limit-down version.

### 47. 临停前排队净方向与逼近速度 `Pre-limit queue net direction & approach velocity`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：Before touching the cap, flow crowds toward it. Net direction of new limit orders in the top ticks below the cap plus cancellation of overhead resistance, combined with approach speed, separates an informed breakout from a squeeze that fades. The +-10% wall reshapes queueing only near the cap.
- **所需字段**：last_price, ap[0..2], bp[0..2], MBO Order.side/price/qty, MBO Cancel.side/price/qty, exchtime, prev_close(adjusted)
- **计算公式**：Proximity zone: 0<(p*-last_price)/p* < 1% AND not yet sealed. Band = top 3 ticks below p* (price-relative: use min(3 ticks, 0.5% band) so low-price 0.01-tick stocks don't get an oversized % band). Per 3s in zone (exchtime<=t): NewBuyNear=sum Order.qty(side=buy, price in band); NewSellNear=sum Order.qty(side=sell, band=overhead resistance); CxlSellNear=sum Cancel.qty(side=sell, band). QueueNet(t)=sum(NewBuyNear-NewSellNear+CxlSellNear). ApproachVel=mean over zone events of delta last_price/delta exchtime (ticks per second toward cap, exchtime in ns->s). Factor=zscore(ApproachVel)*zscore(QueueNet). Mirror band above p_dn for limit-down.
- **聚合到 30min/日频**：Accumulate only while in near-limit zone; emit running QueueNet + mean ApproachVel; daily aggregate over all near-limit episodes.
- **横截面标准化 / 中性化**：Universe = names entering near-limit zone at least once; z-score within board group.
- **预期方向**：Positive: positive queue-net + fast approach -> positive next-day return; stacked-and-unpulled resistance -> weaker.
- **新颖性**：Spatially conditioned on limit distance and fused with approach velocity; captures the behavioral regime shift (resistance-cancel, buy-stacking under the cap) that only exists within a hair of the price cap; not a generic OBI.
- **A股陷阱 / look-ahead**：Window strictly pre-seal near-limit. Use price-relative band for low-price stocks. exchtime (time-of-day ns) for velocity, never localtime. Only MBO up to t.

### 48. 开盘竞价不可撤区失衡跃变 `Opening-auction pre-vs-post 09:20 imbalance jump`
**novelty 4/5 · lowfreq 2/5 · computable True**

- **信息含义 / 机制**：09:15-09:20 cancels allowed, 09:20-09:25 forbidden. Players paint bluff imbalance 09:15-09:20 then cancel before the lock; genuine committed demand persists into the no-cancel window. The imbalance jump across 09:20 and pre-09:20 cancel intensity separate real from fake opening pressure, a uniquely A-share auction-rule signal.
- **所需字段**：total_bid_qty, total_ask_qty, MBO Order.side/qty, MBO Cancel.side/qty, exchtime
- **计算公式**：Gate all by exchtime (time-of-day). 09:15:00-09:20:00 accumulate CxlPre_side=sum Cancel.qty by side and AddPre_side=sum Order.qty by side. Imb_pre=(total_bid_qty-total_ask_qty)/(total_bid_qty+total_ask_qty+1e-9) at last event with exchtime<09:20. Imb_post = same at last event with exchtime<09:25 (do NOT read the 09:25 match/clearing price). Factors: ImbJump=Imb_post-Imb_pre; FakeBuyRatio=CxlPre_buy/(AddPre_buy+1e-9); CommittedImb=Imb_post. Single daily value fixed at 09:25, carried as a next-day feature.
- **聚合到 30min/日频**：Event-level within auction; single value at 09:25 carried forward.
- **横截面标准化 / 中性化**：Cross-sectional z-score across full universe at 09:25.
- **预期方向**：Positive CommittedImb & positive ImbJump -> positive return; high FakeBuyRatio (buy imbalance evaporates before 09:20) -> negative.
- **新颖性**：Directly exploits the 09:20 no-cancel boundary, making cancellation itself the signal; no non-A-share market has this mechanic and baselines never condition on the cancel-lock timestamp.
- **A股陷阱 / look-ahead**：Timestamp strictly with exchtime, not localtime. Some feeds only give aggregate auction snapshots, fall back to total_bid_qty/total_ask_qty. Decays as pure next-day predictor (hence lowfreq=2). Do not leak the 09:25 clearing price into pre-09:20 features.

### 49. 收盘集合竞价机构调仓足迹 `Closing-auction institutional single-order footprint`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：The closing auction is where index/ETF/institutional NAV & rebalance trades execute with minimal signaling. Large single orders (high qty per order_id) with net direction, plus closing-auction share of whole-day volume, reveal institutional accumulation/distribution predicting next-day drift.
- **所需字段**：MBO Trade.Price/Qty/Side/BidId/AskId, volume(cumulative), turnover, exchtime, float_shares(reference)
- **计算公式**：Restrict to 14:57:00-15:00:00 (exchtime). First aggregate Trade.Qty by full order via BidId (buy order) and AskId (sell order) so one large parent order counted once (avoid many-fills inflation). Per-order auction fill F_o=sum Trade.Qty over its id. Q_big=max(cross-sectional 90th pct of F_o that day, 500k CNY / last_price shares). LargeBuyVol=sum F_o(buy orders with F_o>=Q_big); LargeSellVol analog. NetInstFlow=LargeBuyVol-LargeSellVol. CloseVolShare=(volume_1500-volume_1457)/(volume_1500+1e-9). InstFootprint=NetInstFlow/(volume_1500-volume_open+1e-9), gated by CloseVolShare as intensity. Single daily value at 15:00, t+1 predictor.
- **聚合到 30min/日频**：Auction-window accumulation; one value at 15:00.
- **横截面标准化 / 中性化**：Winsorize, z-score across universe; residualize on index-rebalance dates (a CloseVolShare cluster) to keep idiosyncratic footprint.
- **预期方向**：Positive net institutional closing footprint -> positive next-day return; distribution -> negative.
- **新颖性**：Uses order_id-level single-order size distribution WITHIN the closing auction, not day-long big-money net inflow (the discredited zhuli baseline). Auction conditioning + per-order aggregation isolates footprints whole-day thresholds smear out.
- **A股陷阱 / look-ahead**：BidId/AskId aggregation required to size true order magnitude. Separate passive-rebalance days (low alpha) from discretionary flow. Strict t+1 predictor.

### 50. T+1 卖压来源结构因子 `T+1 sell-supply provenance asymmetry`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：Under T+1 shares bought today can't be sold today and most stocks can't be shorted, so ALL intraday sell supply is pre-existing inventory while buy flow is fresh. When aggressive selling is heavily met by fresh replenishing sell limits (holders queuing to exit), distribution persists into next day; thin and quickly refilled by buyers is benign. Decomposes sell-side liquidity into standing-inventory exit intensity.
- **所需字段**：ap[0..2], MBO Trade.Qty/Side, MBO Order.side/price/qty, MBO Cancel.side/price/qty, exchtime, float_shares(reference), margin/short-eligible flag(reference)
- **计算公式**：Exclude/flag margin-shortable names (fresh shorts contaminate sell side). Top-tick band = top 3 ask ticks, price-relative cap 0.5%. Per 3s (exchtime<=t): AggSellFill=sum Trade.Qty(aggressor=sell); SellReplenish=sum Order.qty(side=sell,price in band); SellCxl=sum Cancel.qty(side=sell,band); AggBuyFill=sum Trade.Qty(aggressor=buy). ExitPressure(t)=sum(SellReplenish-SellCxl) / (float_shares); ReplenishRatio(t)=sum SellReplenish/(sum AggBuyFill+1e-9). No symmetric buy version (no short supply). Daily = value at 15:00.
- **聚合到 30min/日频**：Per 3s accumulate by side & top-tick band; running day-to-date; daily=15:00.
- **横截面标准化 / 中性化**：Z-score within price/turnover bucket; the buy/sell asymmetry (replenish vs aggressive-buy fills) is the directional signal.
- **预期方向**：Negative: high persistent replenishing sell supply (holders re-queuing to exit into every bid) -> lower next-day return; heavy sell-cancel -> positive.
- **新颖性**：Explicitly encodes A-share sell supply = inventory only (no intraday round-trip, no shorting). Standard OFI treats buy/sell symmetrically; this deliberately breaks symmetry using the institutional constraint, measuring replenishment provenance via MBO.
- **A股陷阱 / look-ahead**：Exclude/flag shortable/margin names. Price-relative top-tick band. Only MBO up to t.

### 51. tick 绑定流动性压缩因子 `Tick-constrained pinned-spread liquidity`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：For low-price stocks the 0.01 tick is a large fraction of price (0.33% at 3.00 CNY). Spread is floored at one tick and cannot narrow, so the informative variable is not spread (constant) but depth imbalance and how often mid jumps a full tick. Tick-binding discretizes price dynamics predictably, A-share-specific via the absolute 0.01 tick and 100-share lot.
- **所需字段**：ap[0], bp[0], av[0], bv[0], bid_cnt[0], ask_cnt[0], last_price, exchtime
- **计算公式**：Work in integer cents to avoid float error. PinnedFlag when (ap[0]-bp[0])==1 cent. TickBindFrac(t)=fraction of day-to-date events pinned. DepthImb_pinned(t)=mean over pinned events of (bv[0]-av[0])/(bv[0]+av[0]+1e-9). TickJumpUp=count events where bp[0] rose exactly 1 cent; TickJumpDown analog; PinnedPush(t)=sum(TickJumpUp-TickJumpDown) within pinned regime. Single-lot texture=bv[0]/bid_cnt[0] vs av[0]/ask_cnt[0]. Emit DepthImb_pinned and PinnedPush gated by TickBindFrac. Exclude limit-locked events (spread undefined). Daily EOD.
- **聚合到 30min/日频**：Per 3s flag pinned, accumulate depth imbalance & tick-jump counts; running means; daily EOD.
- **横截面标准化 / 中性化**：Rank WITHIN the low-price/high-tick-binding subuniverse (meaningless for high-price names); bucket by price.
- **预期方向**：Positive: positive pinned depth imbalance + net upward tick jumps -> positive next-day return (accumulation the constant spread cannot reveal).
- **新颖性**：Conditions on the tick actually BINDING; standard spread/liquidity factors treat spread as continuous and would see a constant. Signal lives in depth and discrete tick-jump direction that only matter at the spread floor, a pure tick-institution artifact.
- **A股陷阱 / look-ahead**：Integer-cent comparison for one-tick detection. Exclude limit-locked periods. Price bucketing to avoid mixing regimes; effect concentrated in low-price names.

### 52. 涨跌停附近隐藏/冰山买盘 `Hidden/iceberg buying near the limit`
**novelty 3/5 · lowfreq 2/5 · computable True**

- **信息含义 / 机制**：Near limit-up some institutions want in without revealing size (avoid triggering chasers or a premature seal), behaving iceberg-like: visible bv[0] stays modest but executed buy volume at the level greatly exceeds the visible queue (displayed depth continually refreshed by hidden refills). Execution >> visible depth near the cap reveals concealed demand predicting continuation.
- **所需字段**：last_price, bp[0], bv[0], bid_cnt[0], MBO Trade.Price/Qty/Side, MBO Order.side/price/qty, exchtime, prev_close(adjusted)
- **计算公式**：Near-limit zone (p*-last_price)/p* < 2% and not sealed. Per 3s (exchtime<=t): VisibleBid=bv[0] at window start; ExecAtBid=sum Trade.Qty(aggressor=sell hitting bp[0]); count new buy Order.qty(price==bp[0]) refilling same window. HiddenRatio(t)=sum ExecAtBid/(sum VisibleBid_snapshots+1e-9). Iceberg flag when HiddenRatio>>1 while bid_cnt[0] small (few orders yet high exec = large hidden per order). Factor=HiddenRatio gated on near-limit and small displayed depth. Mirror at bp near limit-down (hidden selling, negative). Daily EOD aggregate.
- **聚合到 30min/日频**：Per 3s accumulate exec vs displayed at top bid in near-limit zone; timepoint & daily.
- **横截面标准化 / 中性化**：Rank within near-limit universe, board bucket.
- **预期方向**：Positive: high hidden buying absorption near limit -> positive next-day return; symmetric hidden selling near limit-down -> negative.
- **新颖性**：Combines near-limit concealment motive with an execution-vs-displayed-depth iceberg detector using per-level bid_cnt granularity; reconstructs hidden depth from MBO exec vs snapshot without an explicit iceberg field.
- **A股陷阱 / look-ahead**：3s snapshot misses intra-window refills so HiddenRatio is a lower bound (fine for ranking only, hence lowfreq=2). Requires bid_cnt. Do not misread a genuinely deep visible queue as hidden.

### 53. 跌停排队卖单净方向与撤单弃卖 `Limit-down queue net direction & panic-cancel capitulation`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：Mirror of the limit-up game but T+1-asymmetric and more informative on the downside: at limit-down all sellers are inventory holders (no shorts) queuing to escape at the floor. A growing wall = capitulation continues (bearish); a shrinking/withdrawn wall (holders give up exiting at the floor, or dip buyers absorb) predicts next-day reversal. With no short supply, a shrinking floor queue is a strong inventory-exhaustion signal.
- **所需字段**：last_price, av[0], ap[0], MBO Order.side/price/qty, MBO Cancel.side/price/qty, MBO Trade.Price/Qty/Side, exchtime, prev_close(adjusted), float_shares(reference), short-eligible flag(reference)
- **计算公式**：p_dn=round(prev_close_adj*(1-cap),2), cap by board. AT_LIMIT_DOWN when last_price==p_dn. Exclude shortable names (fresh shorts contaminate the wall). From MBO at p_dn (exchtime<=t): SellAdd=sum Order.qty(side=sell,price==p_dn); SellCxl=sum Cancel.qty(side=sell,price==p_dn); BuyAbsorb=sum Trade.Qty(aggressor=buy,Price==p_dn). SellWall S_t=av[0]. WallGrowth(t)=sum(SellAdd-SellCxl-BuyAbsorb)/float_shares; CapitulationCancel(t)=sum SellCxl/(sum SellAdd+1e-9); plus NumDownOpens (limit-down analog of the open-state-machine). Daily = last valid AT_LIMIT_DOWN value / EOD.
- **聚合到 30min/日频**：Per 3s accumulate at floor price; timepoint & EOD.
- **横截面标准化 / 中性化**：Conditional on limit-down universe; rank within board group.
- **预期方向**：Growing wall + failing buy absorption -> negative next-day; high CapitulationCancel + rising BuyAbsorb (holders stop trying to exit, buyers absorb) -> positive next-day reversal.
- **新颖性**：Exploits no-short/T+1 inventory-only sell supply at the floor: sell-queue withdrawal as exhaustion is a signal that only exists because the wall cannot be replenished with fresh shorts. Distinct from a symmetric limit factor.
- **A股陷阱 / look-ahead**：Adjusted p_dn; board-specific cap (ST +-5%, STAR/ChiNext +-20%). Only MBO up to t. Exclude shortable names where fresh shorts contaminate the wall.

### 54. 大单挂单存活时长买卖不对称 `Large-order resting-lifetime asymmetry via order_id survival`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：Fleeting large orders (added then cancelled within seconds) are a liquidity mirage/spoof; large orders that rest and eventually fill are genuine commitment. Survival analysis of the order lifecycle separates the two. Under T+1 & no-short, sell-side large orders are inventory-driven; the asymmetry in commitment lifetime between sides reveals which side has patient conviction and predicts next-day drift.
- **所需字段**：MBO Order.side/price/qty/order_id, MBO Cancel.order_id/side/qty, MBO Trade.Qty/Side/BidId/AskId, last_price, exchtime, float_shares(reference)
- **计算公式**：Continuous session only (no auction cancel window). Match each limit Order.order_id to its terminal event: full fill via Trade cumulative filled qty on its BidId/AskId reaching Order.qty, or Cancel of the order_id. Lifetime L=(terminal_exchtime - add_exchtime)/1e9 seconds (exchtime is time-of-day ns; both endpoints same day so subtraction valid). Use only orders whose terminal event has exchtime<=t (right-censor still-resting orders: exclude, do NOT peek at future termination -> no look-ahead). Large order: qty>=Q90 (cross-sectional 90th pct of order qty that day) OR notional>=300k CNY. Define: BuyFleetShare=#large buy orders cancelled with L<3s / #large buy orders; SellFleetShare analog; CommitLifeBuy=median L of large buy orders that fully FILLED; CommitLifeSell analog. Factors: LifeAsym(t)=(CommitLifeBuy-CommitLifeSell)/(CommitLifeBuy+CommitLifeSell+1e-9); SpoofAsym(t)=SellFleetShare-BuyFleetShare. Daily = value at 15:00.
- **聚合到 30min/日频**：Per 3s match terminated orders (exchtime<=t) into survival buckets; accumulate day-to-date medians/shares; daily=15:00.
- **横截面标准化 / 中性化**：Winsorize then z-score across universe; bucket by turnover (order-count density affects median stability).
- **预期方向**：Positive: patient committed buying (long buy commit-lifetime, low buy fleeting share) relative to sell -> positive next-day return.
- **新颖性**：Uses full order_id add->fill/cancel survival timing (a lifecycle survival statistic), orthogonal to snapshot factors and to crude cancel/add ratios; distinguishes spoof-like fleeting depth from patient committed depth per side.
- **A股陷阱 / look-ahead**：Handle partial fills via cumulative filled qty per id (full fill only when cumulative==Order.qty). Right-censor orders still resting at t (exclude) to avoid look-ahead. exchtime ns for lifetime, never localtime. Continuous session only. Sparse large-order counts on illiquid names -> shrink toward bucket median or require min-count.

---

## 视角七、横截面相对 / 引领滞后

*利用全 universe 每 3s 同步，构造特质流、共动、引领-滞后、拥挤等相对/网络型因子。*

> 状态：已对抗式评审精选 · 本视角 10 个因子

### 55. 特质主动流 Alpha（对市场+行业流残差化） `Idiosyncratic Taker-Flow Alpha`
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：把个股逐笔主动成交流拆成系统性部分(随大盘/行业共动,多为被动指数化/风格化交易,无个股信息)与特质残差(针对该股的知情下单)。特质净买入累积代表私有信息驱动的建仓,预示1日前向超额收益为正。剥离共动后正交于OBI/动量/裸Kyle-lambda。
- **所需字段**：Trade.Qty, Trade.Side, Trade.Price, Trade.Cnt, bp[0], ap[0], volume(前日基准), 行业归属(申万,日频固定), 总市值(市值加权), exchtime
- **计算公式**：【共用定义,所有因子引用】时间基准:一律用exchtime(当日纳秒,time-of-day)做日内窗口/跨标的同步对齐,localtime(epoch纳秒)仅作落地日志、禁止入公式或与exchtime混用。仅取连续竞价段(09:30-11:30/13:00-15:00)事件;集合竞价(09:15-09:25,14:57-15:00)不计入任何日内累积。mid_i(t)=(bp0_i+ap0_i)/2,要求bp0_i>0且ap0_i>0且未单边触板,否则该事件mid=NaN且不参与累积。3s有向主动量STV_i(t)=Σ_{trade∈本3s}Qty·s,s=+1(Trade.Side主动买)/-1(主动卖);标准化流sof_i(t)=STV_i(t)/ADV1s_i,ADV1s_i=max(前一交易日成交量/前一日连续竞价3s事件数, ε_i),ε_i=行业中位ADV1s×1%防除零;行业leave-one-out流G_s(t,-i)=(Σ_{j∈s}sof_j - sof_i)/(n_s-1),n_s>=5否则NaN;市场流M(t)=Σ_i w_i·sof_i,w_i=前日收盘总市值权重(归一)。【本因子】截至出值时点t用扩展窗(仅t及之前、有效3s样本>=100)做时序回归 sof_i(τ)=α_i+β^M_i·M(τ)+β^G_i·G_s(τ,-i)+eps_i(τ), τ<=t;特质流eps_i=残差。F1_i(t)=半衰~30min的EWMA_{09:30..t}[eps_i(τ)](抑早盘噪声,等价截至t累积特质净买入)。β用扩展窗、ADV用前日,无未来。
- **聚合到 30min/日频**：8个固定时点(09:30/10:00/.../14:30)各出一值EWMA_{09:30..t}[eps_i],聚合成日频。beta与ADV用截至t/前日,残差用同期M/G(非未来),累积只到出值时点。
- **横截面标准化 / 中性化**：同一时点先按行业demean(消T+1买盘常数偏置与行业beta残留),再全市场z-score,winsorize到±3σ。
- **预期方向**：正: 累积特质净买入越大, 1日前向收益越高。
- **新颖性**：基线'大单净流入/主力净额'是绝对有向流,混入随大盘一起买的被动共动;本因子显式回归剔除市场+行业共动只留个股私有信息流,是对'净流入'信息含量的重构,正交于OBI、动量、Kyle-lambda。
- **A股陷阱 / look-ahead**：T+1使主动买盘系统性偏多->必须横截面/行业demean;触涨跌停单边盘口使mid失真->剔除触板事件;小盘3s成交稀疏使beta噪声大->设最小成交笔数(Trade.Cnt)门槛与样本>=100;集合竞价段不计入累积。

### 56. 订单流共动性 R²（拥挤/羊群显性度量） `Order-Flow Commonality R-squared`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：用F1回归的可解释方差比R²衡量个股主动流被市场+行业流'带节奏'的程度。高R²=该股交易被指数/风格资金主导(被动配置、羊群、拥挤),缺私有信息且易反转;低R²且自身有强流=特质知情。作为拥挤/反转信号,高共动预示1日收益偏低。
- **所需字段**：Trade.Qty, Trade.Side, bp[0], ap[0], 行业归属, 总市值, 前日换手率(中性化), exchtime
- **计算公式**：复用F1同一扩展窗时序回归(τ<=t,有效样本>=100)。R²_i(t)=1-Var_τ(eps_i)/Var_τ(sof_i),若Var_τ(sof_i)<ε则R²=NaN;附特质强度IdioStr_i(t)=std_τ(eps_i)。因子取R²_i(t);方向化变体F2b=R²_i·sign(EWMA_{09:30..t}M(τ))。
- **聚合到 30min/日频**：R²_i(t)与IdioStr_i均为时点值(扩展窗估计天然平滑),8时点出值聚合日频。R²仅用t及之前样本估计,无未来。
- **横截面标准化 / 中性化**：先对ln总市值与前日换手率做横截面回归取残差(大盘天生高共动,否则退化为size因子),再全市场z-score。
- **预期方向**：负: R²越高(越拥挤/被动)->1日前向收益越低; 低R²+高特质强度为正。
- **新颖性**：commonality多用于流动性溢价研究(Karolyi/Chordia),几乎无人用逐笔主动流的R²做个股拥挤度选股;做成低换手横截面因子且与F1天然配对(方向×强度)是新重构。
- **A股陷阱 / look-ahead**：市值与R²强相关->不中性化会退化为size因子;行业成分过少时G不稳->设最小成分数n_s>=5;触板期回归失真剔除;Var(sof)接近0(极稀疏股)->置NaN。

### 57. 订单流引领度（信息领先者） `Order-Flow Leadership`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：利用全universe每3s同步,检验个股3s有向流/收益是否领先其行业(剔除自身)。领先者最先获取并交易信息,信息随后扩散到跟随者;领先者处于信息前沿,其当前有向流方向对自身1日收益有延续性(信息尚未完全定价)。
- **所需字段**：Trade.Qty, Trade.Side, bp[0], ap[0], 行业归属, exchtime(同步时钟)
- **计算公式**：(1)个股信号x_i(τ)=sof_i(τ)(或r_i(τ)=ln(mid_i(τ)/mid_i(τ-1)),需mid双边有效);行业目标y_s(τ,-i)=G_s(τ,-i)。跨标的对齐必须以exchtime分3s桶,停牌/无成交事件置NaN不参与corr。(2)扩展窗前导互相关LeadBeta_i(t)=Σ_{k=1..5}[corr_τ(x_i(τ),y_s(τ+k,-i)) - corr_τ(x_i(τ),y_s(τ-k,-i))],窗内有效对数>=100否则NaN;>0表i领先行业(3~15s)。(3)F3_i(t)=行业内rank(LeadBeta_i)线性映射到[-1,1] × z_横截面(F1_i(t))(方向×强度),乘积后全市场z-score。
- **聚合到 30min/日频**：LeadBeta用截至t扩展窗(仅历史/已发生事件)估计,方向项F1累积到t,8时点出值聚合日频。出值时不使用未来事件。
- **横截面标准化 / 中性化**：LeadBeta行业内rank映射到[-1,1];方向项用F1的横截面z值;乘积后全市场z-score+winsorize。
- **预期方向**：正: 领先且净买->1日收益高; 领先且净卖->低。
- **新颖性**：Lo-MacKinlay式lead-lag多在日频/收益层(大盘领先小盘);此处在3s逐笔订单流层用同步截面leave-one-out前导互相关识别'知情下单领先者'并与特质流方向相乘,前人极少在逐笔流上做且与动量正交。
- **A股陷阱 / look-ahead**：3s同步须用exchtime(time-of-day)对齐而非localtime;停牌股制造错位->置NaN;小盘互相关不稳->长窗+行业内rank+有效对数门槛;触板剔除。

### 58. 跟随者收敛缺口（相对欠反应） `Laggard Convergence Gap`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：F3的对偶。行业/领先者已反应的信息,跟随者(滞后股)价格尚未体现->存在收敛压力。若行业(剔除自身)累积上涨,而个股按其历史lead-lag-beta折算后仍明显落后,则1日内向行业收敛->正收益。捕截面欠反应/信息扩散滞后。
- **所需字段**：bp[0], ap[0], 行业归属, Trade.Qty, Trade.Side(F1过滤用), 总市值, exchtime
- **计算公式**：(1)扩展窗估滞后beta:r_i(τ)=a_i+Σ_{k=0..5}b_{i,k}·RG_s(τ-k,-i)+u_i(τ),RG_s(τ,-i)=行业leave-one-out对数收益(市值加权),Σb_i=Σ_k b_{i,k}(对行业冲击总敏感度,仅用t及之前样本估计);(2)累积CG_i(t)=Σ_{09:30..t}RG_s(τ,-i),CR_i(t)=Σ_{09:30..t}r_i(τ);(3)收敛缺口Gap_i(t)=Σb_i·CG_i(t)-CR_i(t),>0=跟随行业冲击后仍欠反应。仅对跟随型(Σb_i显著>0)输出,可用Σb_i加权;用F1过滤:Gap>0但F1_i<0(特质净卖,疑个体真利空)剔除。触板致CR截断事件剔除。
- **聚合到 30min/日频**：Σb_i用截至t扩展窗(仅t及之前样本)估计;CG/CR从09:30累积到t;8时点出值聚合日频,无未来。
- **横截面标准化 / 中性化**：行业内z-score(缺口幅度依行业),再全市场z-score+winsorize。
- **预期方向**：正: 欠反应缺口越大->1日收益越高(向行业收敛)。
- **新颖性**：把'相对强弱/reversal'重构为基于逐笔lead-lag-beta的欠反应缺口而非裸相对收益;beta来自订单流领先结构,抓信息扩散滞后而非单纯低beta。
- **A股陷阱 / look-ahead**：个股因个体真利空而跌(非滞后)会误判->用F1特质流过滤;涨跌停使CR截断->触板剔除;beta历史估计防look-ahead。

### 59. 激进买入拥挤集中度 `Aggressive-Buy Crowding Concentration`
**novelty 3/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：利用同步截面度量'主动买压在横截面上是否高度集中'。某股在其行业内吸走不成比例的主动买量(集中度高),多为散户/趋势资金短期扎堆(A股散户羊群+T+1助推追涨),属拥挤交易,1日内易反转->负收益。强调相对份额与行业集中regime,而非自身流的绝对大小。
- **所需字段**：Trade.Qty, Trade.Side, 行业归属, 总市值/前日换手(中性化), exchtime
- **计算公式**：(1)行业内当日累积主动买量ABV_i(t)=Σ_{09:30..t}max(STV_i,0),剔除触涨停封板事件(封板STV极端失真);(2)份额share_i(t)=ABV_i/max(Σ_{j∈s}ABV_j, ε),n_s>=8否则NaN;(3)行业买流集中度HHI_s(t)=Σ_{j∈s}share_j²,按成分数去偏HHI_adj=(HHI_s-1/n_s)/(1-1/n_s);(4)F5_i=share_i×HHI_adj_s(在集中的行业里且自己占大头才算真拥挤)。
- **聚合到 30min/日频**：各3s累积主动买量从09:30累积到t;F5为时点值(份额天然低频可比);8时点出值聚合日频,无未来。
- **横截面标准化 / 中性化**：对ln总市值/前日换手中性化(大股天然份额高)后z-score+winsorize。
- **预期方向**：负: 截面拥挤度越高->1日前向收益越低(反转)。
- **新颖性**：基线拥挤度多用持仓/因子暴露相关性;此处用同步逐笔主动买流的横截面份额+行业去偏HHI构造微结构拥挤,契合A股散户追涨扎堆的独特反转机制。
- **A股陷阱 / look-ahead**：T+1使买量普遍偏高->用相对份额抵消;涨停封板制造极端share->剔除触板;小行业HHI偏高->成分数去偏+n_s门槛。

### 60. 相对冲击成本分位及其改善 `Relative Impact-Cost Percentile and Improvement`
**novelty 3/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：用10档快照重建可执行冲击成本,放到行业横截面取分位,度量个股相对同行的流动性。相对最贵(高冲击)股带流动性溢价->收益高;相对流动性'改善'(深度相对同行变厚、冲击相对下降)常先于聪明钱建仓与后续上涨。做成相对量以剥离全市场流动性周期。
- **所需字段**：ap[0..9], av[0..9], bp[0..9], bv[0..9], 行业归属, turnover(前日,标定名义额), 总市值, exchtime
- **计算公式**：(1)规模标定名义额X_i=前日成交额/前日连续竞价3s事件数×c(c全市场同值);按X_i沿ap[0..9]/av[0..9]累积吃单得加权买入均价P^buy_i,买冲击IC^b_i=P^buy_i/mid_i-1;对称沿bp[0..9]/bv[0..9]得IC^s_i;若X_i超过10档可用量或单边触板则该事件置NaN;IC_i=(IC^b_i+IC^s_i)/2,取时点内各3s中位数medIC_i(t)。(2)行业分位pct_i(t)=rank_{j∈s}(medIC_j)/n_s∈[0,1]。(3)改善量dLiq_i(t)=pct_i(前一交易日同一时点)-pct_i(t)(分位下降=相对变好,用前日值无未来)。F6a=pct_i(对ln市值中性化后z);F6b=dLiq_i(多日EWMA后z-score+winsorize)。
- **聚合到 30min/日频**：时点内对各3s的IC取中位数;水平F6a与差分F6b(建议多日EWMA降换手)8时点出值聚合日频。X用前日、差分用前日同时点值,无未来。
- **横截面标准化 / 中性化**：pct已∈[0,1];F6a对ln市值中性化后z;F6b直接z-score+winsorize。
- **预期方向**：F6a正(相对越不流动->流动性溢价->收益高); F6b正(相对流动性改善->建仓->收益高)。
- **新颖性**：冲击成本本身接近基线,但'行业横截面分位'+'相对改善的时间差分'把它从水平量重构为相对/动态量,剥离全市场流动性beta,抓相对建仓,正交于裸Amihud。
- **A股陷阱 / look-ahead**：触板单边簿->冲击无穷,剔除;X_i须按个股规模标定否则大盘永远'更流动';停牌无簿置NaN;F6b换手较高建议多日EWMA。

### 61. 相对队列存活/撤单不对称 `Relative Queue-Life and Cancel Asymmetry vs Peers`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：用MBO生命周期(order_id<->cancel<->trade)重建队列,计算买侧挂单相对卖侧的'耐心度'(存活久、撤单率低=坚定买家),再相对行业均值取残差。相对同行买盘更耐心、卖盘更易撤=净坚定买压/悄悄吸筹,预示1日正收益。相对化剥离全市场撤单风格与tick制度共性。
- **所需字段**：Order.order_id, Order.side, Order.price, Order.qty, 委托类型, Cancel.order_id, Cancel.side, Cancel.qty, Trade.BidId, Trade.AskId, Trade.Qty, 行业归属, exchtime
- **计算公式**：(1)用order_id做Order(add)↔Cancel(被撤order_id)↔Trade(BidId/AskId命中)生命周期匹配,只统计截至t前已闭合(全撤或全成)且为限价单的委托(市价/本方最优等按委托类型排除;SSE缺类型版本仅用可匹配的add/cancel/trade的id、不依赖类型字段);存活life=闭合exchtime-下单exchtime(全用exchtime,禁用localtime)。(2)买侧撤单率CR^b_i=Σ撤单量/Σ挂单量,卖侧CR^s_i;买侧中位存活L^b_i、卖侧L^s_i(当日累积,可EWMA)。(3)不对称Asym_i=z(CR^s_i-CR^b_i)+λ·z(L^b_i-L^s_i)(买更耐心=撤少存活久为正,λ~1)。(4)相对行业残差rAsym_i=Asym_i-mean_{j∈s,≠i}Asym_j。
- **聚合到 30min/日频**：生命周期统计只计已闭合(已撤/已成)且发生在t之前的限价委托,当日累积/EWMA到时点;8时点出值聚合日频。未闭合挂单不计入,无未来。
- **横截面标准化 / 中性化**：已做行业leave-one-out去均值(公式内);rAsym再全市场z-score+winsorize。
- **预期方向**：正: 相对同行买盘更坚定(撤少、存活久)->1日收益高。
- **新颖性**：撤单率是基线但'裸';此处做买/卖不对称+存活时长几何量+行业相对残差三重重构,需真正的order/cancel/trade生命周期匹配,工程门槛高、前人少做,且与价量因子高度正交。
- **A股陷阱 / look-ahead**：09:20-09:25不可撤单段剔除;SSE部分版本委托类型缺失->只用id匹配不依赖类型;涨停封板排队制造超长life->触板剔除;市价/本方最优单按类型排除以免污染life;life须用exchtime。

### 62. 特质流确认的特质动量 `Flow-Confirmed Idiosyncratic Momentum`
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：横截面上把'特质价格变动'与'特质订单流'做一致性交互。特质收益上行且由特质净买入确认(同号)=信息驱动,1日延续(正);特质价涨但特质流为卖(背离)=无量上涨/被动裹挟,1日反转(负)。用同步截面同时定义价与流的特质分量,解决量价背离的截面判别。
- **所需字段**：bp[0], ap[0], Trade.Qty, Trade.Side, 行业归属, 总市值, exchtime
- **计算公式**：(1)特质收益:r_i(τ)对RG_s(τ,-i)扩展窗回归得β^ret_i(仅用t及之前样本),CR_i(t)=Σ_{09:30..t}r_i(τ),CG_i(t)=Σ RG_s,idioR_i(t)=CR_i-β^ret_i·CG_i;(2)特质流F1_i(t)(见F1);(3)一致性交互Conf_i=sign_agree·sqrt(|z(idioR_i)|)·sqrt(|z(F1_i)|),sign_agree=+1若idioR_i与F1_i同号否则-1(先取符号再对绝对值开方,避免负数开方)。F9_i=Conf_i。
- **聚合到 30min/日频**：β^ret用截至t扩展窗历史估计;idioR、F1累积到t;8时点出值聚合日频,无未来。
- **横截面标准化 / 中性化**：idioR、F1各自先横截面z,再合成Conf,最后对Conf全市场z-score+winsorize。
- **预期方向**：正: 特质涨且流确认->延续(正); 背离(价涨流卖)->Conf为负->预示回落。
- **新颖性**：量价背离多为日频/整体量;此处是'特质价×特质流'的截面一致性交互(两者均已剔除市场+行业共动)、几何压缩,正交于裸动量与OBI,抓真正的信息确认。
- **A股陷阱 / look-ahead**：T+1使流偏买->横截面demean;触板idioR截断剔除;开方前先去符号避免负数开方;无融券使卖侧特质流偏弱需注意。

### 63. 对市场流冲击的相对反应速度 `Relative Response-Speed to Market Flow Shocks`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：用同步截面识别全市场订单流冲击事件(|M(t)|大),度量个股是当期就反应还是滞后才反应。反应慢的个股信息处理慢、存在欠反应漂移:市场冲击方向会在1日内继续兑现->可预测漂移。区分'快反应=已定价'与'慢反应=未定价漂移'。
- **所需字段**：bp[0], ap[0], Trade.Qty, Trade.Side, 总市值(算M权重与中性化), 行业归属(中性化), exchtime
- **计算公式**：(1)市场流M(t)(共用定义);冲击集S={τ:|M(τ)|>Q90(|M|;扩展窗,仅用τ及之前已发生样本)}。(2)在S上估当期/滞后反应:r_i(τ)=b0_i·M(τ)+b1_i·M(τ-1)+b2_i·M(τ-2)+e_i(τ),要求i在该τ有有效mid与成交否则该样本剔除,|S|有效样本>=50否则NaN。(3)慢度Slow_i=(b1_i+b2_i)/(|b0_i|+|b1_i|+|b2_i|)∈约[0,1],分母<ε置NaN。(4)近端累积市场冲击CM(t)=Σ_{最近30min(exchtime窗)}M(τ)。F10_i=Slow_i×CM(t)。
- **聚合到 30min/日频**：b0/b1/b2与Q90用扩展窗(仅t及之前已发生样本)估计;CM累积最近30min;8时点出值聚合日频,无未来。
- **横截面标准化 / 中性化**：Slow横截面rank;对ln市值中性化(小盘天生滞后易混size);乘CM后全市场z-score+winsorize。
- **预期方向**：正: 慢反应且近端市场净买(CM>0)->1日随市场方向继续漂移(正); 市场净卖时慢反应者为负。
- **新颖性**：利用3s同步专门估计'对系统性流冲击的反应时滞'做欠反应漂移,是相对反应速度的微结构刻画,不同于低beta/size,抓信息扩散速度异质性,前人少用逐笔流冲击事件研究法。
- **A股陷阱 / look-ahead**：停牌/触板股在冲击事件缺反应->NaN;小盘滞后与size混淆->中性化;冲击分位Q90用扩展窗(已发生样本)避免未来分位;集合竞价段不计。

### 64. 相对深度韧性（顶档回补速度对同业残差） `Relative Depth Resilience (Peer-Relative Replenishment Speed)`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：用MBO重建盘口顶档队列,度量顶档被主动成交吃掉后的深度回补速度(liquidity resilience)。买侧顶档被吃后被同价新委托快速补回=存在隐性/耐心买盘(冰山、算法拆单持续补bid),是悄悄吸筹的微结构脚印,预示1日正收益。与F6(静态冲击成本)、F7(委托生命周期)机制不同:这是盘口层面的动态深度恢复。
- **所需字段**：Order.order_id, Order.side, Order.price, Order.qty, Cancel.order_id, Trade.Qty, Trade.Side, Trade.Price, bp[0], bv[0], ap[0], av[0], 行业归属, 总市值, exchtime
- **计算公式**：(1)冲击消耗事件:某3s内主动成交吃掉bp0(或ap0)档累计量>=该档冲击前挂量的γ(γ~0.5,用MBO重建的冲击前顶档量)。(2)冲击后回补:Recov_i=median_{冲击事件}[ (被吃档在其后Δ(=后续1~3个3s事件)内经Order(add,同价或更优)补回的量) / 被吃量 ],跨事件定序全用exchtime;分买侧Recov^b_i、卖侧Recov^s_i。(3)买盘韧性优势RA_i(t)=z(Recov^b_i)-z(Recov^s_i)(买侧补得快=隐性买盘)。(4)相对行业残差rRA_i(t)=RA_i-mean_{j∈s,≠i}RA_j,当日累积/EWMA到t。F_i=rRA_i。冲击事件数<n_min(约20)则NaN;触板单边簿/集合竞价段剔除。
- **聚合到 30min/日频**：冲击/回补统计仅计发生在t之前的事件,当日累积或EWMA到时点;8时点出值聚合日频,无未来。
- **横截面标准化 / 中性化**：行业leave-one-out残差(公式内)后对ln总市值中性化(大盘天然回补快),再全市场z-score+winsorize。
- **预期方向**：正: 相对同行买侧顶档回补越快(隐性买盘越充裕)->1日收益越高。
- **新颖性**：liquidity resilience(冲击后深度回补速度)学术上有概念但极少做成个股横截面选股因子;此处用MBO'同价add回补量/被吃量'精确度量,加买卖不对称与行业相对残差,正交于depth/spread/Amihud以及F6(静态冲击)与F7(委托生命周期)。
- **A股陷阱 / look-ahead**：触板封板致回补无穷/为0->剔除;集合竞价段剔除;停牌置NaN;冰山/隐藏单不可直接见,以同价Order(add)回补量近似,须承认低估但横截面相对可比;小盘冲击稀疏->设n_min门槛;跨事件回补必须用exchtime定序防错位。

---

## 视角八、日内路径 / 订单流演化

*把当日 3s 桶视为时间序列，刻画路径效率、领先-衰竭、加速度、持续性等演化结构。*

> 状态：已对抗式评审精选 · 本视角 7 个因子

### 65. 签名订单流路径效率(有符号) `Signed Order-Flow Path Efficiency`
**novelty 5/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：Kaufman efficiency ratio on signed order flow: net displacement over total churn energy separates a directional accumulation campaign (efficiency ~1, informed) from back-and-forth churn (efficiency ~0, noise). High efficiency plus net-buy sign is a committed one-way campaign whose information persists next day; the same net notional built via churn does not.
- **所需字段**：Trade(qty, aggressor side), Snapshot.exchtime, ADV20
- **计算公式**：COMMON: trading-second tau_e=(exchtime-09:30:00)/1e9 if exchtime<=11:30 else 7200+(exchtime-13:00:00)/1e9; keep continuous-auction events exchtime<=t; per event q_e=sum(+qty if aggressor buy else -qty), Q_e=sum qty, DROP buckets Q_e=0; eps=1e-6*ADV20. STEP: NetPath=|sum_e q_e|; Gross=sum_e|q_e|+eps; signed efficiency F=(sum_e q_e)/Gross in [-1,1]. Small-sample de-bias: F <- F*min(1,N/50) with N=kept bucket count (efficiency is upward biased for small N). Geometric sibling: with C_e=sum_{j<=e}(q_j/ADV20), Eff_geo=|C_last-C_first|/(sum_e|C_e-C_{e-1}|+eps) signed by sign(sum q_e).
- **聚合到 30min/日频**：3s signed buckets -> cumulative net / cumulative absolute-net -> small-sample shrink. Recomputed each of the 8 timepoints on exchtime<=t.
- **横截面标准化 / 中性化**：Already in [-1,1]; MAD-winsorize +/-3.5, z-score, neutralize ln(mktcap)+industry (residual). Residualize against the persistence factor and the net-flow baseline before pooling (efficiency=amplitude ratio vs autocorr=time correlation).
- **预期方向**：positive (efficient directional net buy -> next-day positive; efficient net sell -> negative)
- **新颖性**：Fractal/trend-efficiency migrated to signed order flow; reconstructs the net-inflow baseline into a campaign-vs-churn information measure orthogonal to net-flow level and price momentum.
- **A股陷阱 / look-ahead**：Zero-trade buckets dropped so Gross is not deflated; small-N upward bias fixed by min(1,N/50) shrink plus N_min=20 gate; T+1: for long-only books weight the net-buy side, treat efficient net-sell cautiously (may be forced-inventory liquidation, not information); board-sealed sessions -> NaN, single-list touched-limit names.

### 66. 价流路径领先-衰竭度 `Price-Flow Lead / Exhaustion`
**novelty 5/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：A healthy advance is pushed by sustained active buying. If price is still rising near t but the slope of cumulative net buying has rolled over (price leads flow), buying is exhausted and next-day mean-reverts; if flow accumulates faster than price has moved (flow leads price), the move is under-discounted and continues. Factor = near-term flow-trend minus price-trend on standardized scales.
- **所需字段**：Trade(qty, aggressor side), Snapshot.last_price (trade-updated), Snapshot.exchtime, ADV20
- **计算公式**：COMMON as above (trading-second tau_e, continuous-auction, drop Q_e=0, eps=1e-6*ADV20). Cumulative paths on kept buckets<=t: C_e=sum_{j<=e}(q_j/ADV20); R_e=last_price_e/last_price_first-1 using only trade-updated last_price (stale-quote buckets excluded from the price slope). Near window W={e: tau_e in (tau_t-1800, tau_t]}. OLS on W: fSlope=slope(C_e~tau_e), pSlope=slope(R_e~tau_e). F=z(fSlope)-z(pSlope) with z from cross-sectional std at the timepoint. F>0 flow-leads (accumulation), F<0 price-leads (exhaustion). Emit exhaustion dummy=1[F<0 AND R_t>0].
- **聚合到 30min/日频**：3s buckets -> two cumulative paths -> dual OLS slope on last 30 trading-min -> standardized difference. Recomputed each timepoint, strictly exchtime<=t.
- **横截面标准化 / 中性化**：MAD-winsorize +/-3.5, z-score, neutralize ln(mktcap)+industry; additionally neutralize against intraday return R_t so the increment is the lead/lag structure not the return level. Emit exhaustion dummy separately for non-linear models.
- **预期方向**：positive (flow leads price -> next-day positive; price leads flow / exhaustion -> negative)
- **新颖性**：Upgrades static price-volume divergence to a path-level lead/lag between flow slope and price slope, explicitly modeling exhaustion; orthogonal to pure momentum/reversal.
- **A股陷阱 / look-ahead**：Price slope truncated at limit boards (tick=0.01, no trades when sealed) -> drop sealed buckets, single-list touched-limit names; W with <N_min=20 buckets -> NaN; must use trade-updated last_price not stale quotes; at 09:30 the 30-min window is nearly whole session -> down-weight by available-window fraction; strict exchtime<=t.

### 67. 签名订单流加速度(后-前段斜率差) `Signed Order-Flow Acceleration`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：First derivative of cumulative signed flow is net-buy intensity; the second derivative (acceleration) says whether buying is still building or peaking. Informed capital adds at the margin (positive acceleration); emotional buying spikes then decays (negative acceleration). Acceleration leads price before it is discounted.
- **所需字段**：Trade(qty, aggressor side), Snapshot.exchtime, ADV20
- **计算公式**：COMMON as above. Kept buckets<=t ordered by tau_e; split at trading-time median tau_mid into H1={tau_e<=tau_mid}, H2={tau_e>tau_mid}. OLS slope of (q_e/ADV20)~tau_e on each half -> b1, b2. F=b2-b1. Scale-free option: divide by (|sum(q_e/ADV20)|+eps/ADV20). Require each half >= N_min/2=10 buckets else NaN.
- **聚合到 30min/日频**：3s signed buckets -> split at trading-time median -> per-half time slope -> later minus earlier. Recomputed each timepoint, strictly exchtime<=t.
- **横截面标准化 / 中性化**：MAD-winsorize +/-3.5, z-score, neutralize ln(mktcap)+industry; residualize against path-efficiency and persistence factors to keep the second-order content distinct.
- **预期方向**：positive (accelerating buy pressure -> next-day positive)
- **新颖性**：Baseline net inflow uses only the first moment; taking the second-order (slope-of-slope) of the 3s bucket series captures acceleration vs decay, an orthogonal increment to momentum/reversal.
- **A股陷阱 / look-ahead**：Early timepoints have few buckets -> per-half min-sample gate; time must be trading-seconds so the lunch break is not a fake gap inflating the slope; sealed-board truncation depresses the later half -> drop sealed buckets; strict exchtime<=t.

### 68. 签名订单流持续性(量加权自相关) `Signed Order-Flow Persistence (Volume-Weighted Autocorr)`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：Each 3s net-flow bucket is a series; lag-1 autocorr rho1>0 means persistent flow (informed traders slicing same-direction child orders), rho1<0 means fast reversal (market-making/noise churn). High persistence in the net-buy direction is high information and likely to continue next day; negative persistence flags noise and discounts the direction.
- **所需字段**：Trade(qty, aggressor side), Snapshot.exchtime
- **计算公式**：COMMON as above. On kept buckets<=t use ratio r_e=q_e/Q_e (bounded, halt-robust). Volume-weighted mean rbar=sum_e Q_e r_e/sum_e Q_e. Volume-weighted lag-1 autocorr rho1=sum_{e>=2} w_e (r_e-rbar)(r_{e-1}-rbar)/(sum_e Q_e (r_e-rbar)^2 + eps'), w_e=min(Q_e,Q_{e-1}), eps'=1e-6*sum Q_e. Direction strength D=sum_e q_e/(sum_e|q_e|+eps) in [-1,1]. F=rho1*D. Emit bare rho1 as an orthogonal style.
- **聚合到 30min/日频**：3s ratio series -> volume-weighted lag-1 autocorr -> multiply by signed net-flow direction. Recomputed each timepoint, strictly exchtime<=t.
- **横截面标准化 / 中性化**：rho1 in [-1,1]; winsorize extremes, z-score, neutralize ln(mktcap)+industry. Low correlation with acceleration/efficiency; enters alongside after residualization.
- **预期方向**：positive (persistent same-direction net buying -> next-day positive; persistent net selling -> negative)
- **新颖性**：Baseline has no order-flow time-series structure; volume-weighted intraday autocorrelation separates trend-type informed flow from noise churn, conditioning net inflow into a persistence-vs-reversal label.
- **A股陷阱 / look-ahead**：Original defect: zero-trade buckets q_e=0 inflate autocorr toward +1 -> fixed by dropping Q_e=0 buckets AND volume-weighting so thin buckets barely count; early timepoints -> N_min=20 gate; T+1 asymmetry -> optionally estimate buy-leg and sell-leg persistence separately and combine.

### 69. 日内VWAP相对价路径位置与漂移 `Intraday VWAP-Relative Price Path & Drift`
**novelty 3/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：Where price sits relative to cumulative VWAP measures whether buyers keep paying up. Price spending most of the session above VWAP with an upward-drifting premium means a rising cost center -> strong accumulation likely to continue; price oscillating below VWAP -> seller-dominated. Path-level reconstruction of the single-point VWAP-deviation baseline.
- **所需字段**：Snapshot.turnover (cumulative), Snapshot.volume (cumulative), Snapshot.last_price, Snapshot.exchtime
- **计算公式**：COMMON clock/continuous-auction filter as above. Per event cumVWAP_e=turnover_e/volume_e; dev_e=last_price_e/cumVWAP_e-1. Skip first N0=20 continuous buckets (open cumVWAP noise) and board-sealed buckets. Time weights w_e=tau_e-tau_{e-1} (~3s): AboveFrac=sum_e w_e*1[dev_e>0]/sum_e w_e; devbar=sum_e w_e*dev_e/sum_e w_e; driftSlope=OLS slope(dev_e~tau_e). F=z(devbar)+z(driftSlope). Sibling marginal-cost: ivwap_e=(turnover_e-turnover_{e-1})/(volume_e-volume_{e-1}) only when denominator>0 (else skip), sign of mean(last_price_e-ivwap_e).
- **聚合到 30min/日频**：Cumulative turnover/volume per event -> dev path -> time-weighted mean + time-drift slope. Uses prev-event cumulative values for interval quantities, strictly exchtime<=t.
- **横截面标准化 / 中性化**：dev is unit-free; MAD-winsorize +/-3.5, z-score, neutralize ln(mktcap)+industry; residualize against intraday return so the increment is the cost-center structure not the price level.
- **预期方向**：positive (price persistently above VWAP and drifting up -> next-day positive)
- **新颖性**：Turns single-point VWAP deviation into a path measure (time-above fraction + deviation drift) capturing a rising cost center; snapshot-cumulative based so it is the most low-frequency-stable factor in the set.
- **A股陷阱 / look-ahead**：Open volume tiny -> cumVWAP unstable, fixed by skipping first N0 buckets; interval VWAP divides by volume increment -> skip zero-increment events; at limit boards dev collapses to ~0 constant -> drop sealed buckets, mark touched-limit; turnover/volume same-day cumulative, never cross day; strict exchtime<=t.

### 70. 开盘30分钟流锚定与确认外推 `Opening 30-min Flow Anchor & Confirmation`
**novelty 4/5 · lowfreq 4/5 · computable True**

- **信息含义 / 机制**：The opening 30 minutes of continuous trading is driven by overnight information; its signed net-flow direction anchors the day. Direction alone is not enough - the rest of the day must confirm it. Opening net buy that subsequent buckets keep validating is repeatedly market-tested and carries the highest information for next-day extrapolation.
- **所需字段**：Trade(qty, aggressor side), Snapshot.exchtime
- **计算公式**：COMMON clock/continuous-auction filter (exclude 09:15-09:25 call auction), drop Q_e=0, eps=1e-6*ADV20. Opening window O={e: exchtime in [09:30:00,10:00:00]}. OpenNet=sum_{e in O} q_e/(sum_{e in O} Q_e+eps) in [-1,1]. Confirmation over P={e: 10:00:00<exchtime<=t}: volume-weighted agreement omega=sum_{e in P} Q_e*1[sign(q_e)=sign(OpenNet)]/(sum_{e in P} Q_e+eps) in [0,1]. F=OpenNet*(0.5+omega). At the 10:00 timepoint P is empty -> F=OpenNet*0.5. F is NaN at 09:30 (window incomplete) and must NOT be back-filled from close.
- **聚合到 30min/日频**：Opening 30-min net-buy ratio (anchor) x downstream same-direction volume share (confirmation). P expands each timepoint using exchtime<=t.
- **横截面标准化 / 中性化**：MAD-winsorize +/-3.5, z-score, neutralize ln(mktcap)+industry; residualize against the other signed-flow factors to remove the shared net-direction component so the confirmation content is the increment.
- **预期方向**：positive (opening net buy that is later confirmed -> next-day positive)
- **新颖性**：Couples opening flow direction with a whole-day confirmation ratio into a validated-anchor signal, filtering false breakouts - distinct from a raw early-session net inflow.
- **A股陷阱 / look-ahead**：Use only continuous-auction trades (exclude 09:15-09:25) for the anchor; one-word limit-up open (no continuous trades) -> OpenNet undefined, single-list touched-limit names; NaN at 09:30 with no back-fill (look-ahead); T+1 -> opening sell mixes profit-taking inventory, consider buy/sell legs separately.

### 71. 大单成交-撤单生命周期诚意失衡 `Large-Order Fill-vs-Cancel Lifecycle Sincerity Imbalance`
**novelty 4/5 · lowfreq 3/5 · computable True**

- **信息含义 / 机制**：The feed's unique asset is order_id lifecycle. Genuine accumulation places sizeable passive orders that get filled or keep resting; spoofing/quote-stuffing places large orders cancelled fast. Measuring per side the size-weighted cancel rate of LARGE new limit orders and differencing bid vs ask isolates who is sincere. Sincere bids + flaky asks = real demand -> bullish next day.
- **所需字段**：MBO Order(order_id, side, price, qty), Cancel(order_id, side, qty), Trade(qty, bid_id, ask_id), Snapshot.exchtime
- **计算公式**：For every NEW limit Order i created in continuous-auction events<=t, via order_id lifecycle matching within the day: filled_i=sum Trade.qty where bid_id/ask_id=i; cancelled_i=sum Cancel.qty for i; placed_i=qty_i (count only fills/cancels whose event exchtime<=t, no peeking past t). Restrict to LARGE orders placed_i>=median single-order size of that stock that day (per side). Per side S in {bid,ask}: CancelRate_S=sum_{large i in S} cancelled_i/(sum placed_i+eps); FillThrough_S=sum filled_i/(sum placed_i+eps), eps=1e-6*ADV20. Sincerity imbalance F=(FillThrough_bid-FillThrough_ask)-(CancelRate_bid-CancelRate_ask). Require >=30 large orders per side else NaN.
- **聚合到 30min/日频**：Order_id lifecycle (fill/cancel/rest) aggregated over exchtime<=t, size-gated to large orders, differenced across sides. Recomputed each timepoint.
- **横截面标准化 / 中性化**：F is a unit-free rate difference; MAD-winsorize +/-3.5, z-score, neutralize ln(mktcap)+industry; residualize against the simple cancel-ratio baseline so only the side-differenced, size-conditioned, outcome-aware increment remains.
- **预期方向**：positive (bids sincere / asks flaky -> next-day positive)
- **新颖性**：Goes beyond the simple cancel-ratio baseline: side-differenced, size-conditioned, outcome-aware (fill vs cancel vs rest) using full order_id lifecycle reconstruction - the only factor here that genuinely exploits MBO life-cycle matching.
- **A股陷阱 / look-ahead**：Orders straddling the t boundary: only count fills/cancels with event exchtime<=t (partial lifecycle fine, no peek past t); T+1 short-sale ban means large ask orders are inventory-driven so their cancel behavior is asymmetric - report bid-leg and ask-leg separately for diagnostics; call-auction 09:20-09:25 is no-cancel, exclude; sealed-board days have almost no cancels -> NaN under the 30-order gate.

---

## 3. 跨视角总表

| # | 因子 | 视角 | novelty | lowfreq | 方向 | 状态 |
|---|---|---|---|---|---|---|
| 1 | 存活加权近端深度不平衡 SW-DOBI Survival-Weighted near-tou | 生命周期 | 4 | 4 | + | ★ |
| 2 | 极短命委托买卖不对称 FOA Fleeting Order Asymmetry | 生命周期 | 4 | 3 | + | ★ |
| 3 | 大额委托撤单倾向不对称 LOCA Large-Order Cancel-propensity | 生命周期 | 4 | 4 | + | ★ |
| 4 | 临成交前逃单不对称 FoApproach Flee-on-Approach Asymmetr | 生命周期 | 5 | 3 | + | ★ |
| 5 | 被吃后同价补单速度/吸收不对称 Refill Post-Hit Absorption Asy | 生命周期 | 4 | 3 | + | ★ |
| 6 | 委托存活时长分布位置不对称 Lifespan Order Lifespan Location | 生命周期 | 3 | 4 | + | ○ |
| 7 | 贴近度加权撤单强度不对称 WCI Aggressiveness-Weighted Cance | 生命周期 | 4 | 4 | + | ★ |
| 8 | 深档假墙撤离(掩护试探)不对称 CoverWall Deep Spoof-Wall With | 生命周期 | 5 | 3 | + | ★ |
| 9 | 新委托下单激进度买卖不对称 Order Placement Aggressiveness A | 激进度 | 4 | 4 | Positive: buyers sub | ★ |
| 10 | 跨价意愿（可成交委托强度） Cross-Price Willingness (Marketa | 激进度 | 3 | 4 | Positive: high buy c | ★ |
| 11 | 价内改善报价占比（不跨价插队） Inside-Spread Price-Improvemen | 激进度 | 4 | 3 | Positive: higher net | ★ |
| 12 | 算法拆单强度（子单簇指纹） Algorithmic Slicing Intensity (c | 激进度 | 5 | 4 | Positive: higher buy | ★ |
| 13 | 规模-激进度耦合（大单激进溢价） Size-Aggression Coupling (Agg | 激进度 | 4 | 3 | Positive: large new  | ★ |
| 14 | 显式即时性委托类型强度（深交所委托类型） Explicit Immediacy Order- | 激进度 | 4 | 3 | Positive: higher net | ★ |
| 15 | 新委托挂单深度（离盘口报价距离） New-Order Placement Depth (ti | 激进度 | 3 | 3 | Positive: passive bu | ★ |
| 16 | 主动单扫单深度（多档成交足迹） Aggressor Sweep Depth (multi-l | 激进度 | 5 | 4 | Positive: deeper buy | ★ |
| 17 | 即时性需求对可用深度比（激进度除以流动性） Immediacy Demand vs Avai | 激进度 | 4 | 4 | Positive: buy immedi | ★ |
| 18 | 真实激进度（跨价委托成交/撤单生命周期） Genuine vs Fleeting Aggre | 激进度 | 4 | 4 | Positive: higher sha | ★ |
| 19 | 成交量加权主动成交符号持续性(拆单足迹) Volume-weighted aggressor | 成交流 | 4 | 4 | F1>0(持续净主买)→次日正收益。 | ★ |
| 20 | 对手撤单条件化的毒性净成交额(逆选择加权主动流) Opponent-withdrawal-c | 成交流 | 5 | 4 | F2>0(ask侧不愿补的毒性净主买)→ | ★ |
| 21 | 符号非对称Kyle-λ(方向性冲击几何) Sign-asymmetric Kyle-lamb | 成交流 | 4 | 3 | F3>0(买比卖更推价、抗卖)→次日正收 | ★ |
| 22 | 主动流与已实现收益背离(潜伏吸筹/未兑现涨幅) Aggressive-flow vs rea | 成交流 | 4 | 4 | F4>0(净买多于价涨→欠反应吸筹)→次 | ★ |
| 23 | 主动成交规模集中度不对称(买卖成交HHI/熵) Aggressive trade-size  | 成交流 | 4 | 4 | F5>0(买侧规模分布比卖侧更集中/尾更 | ★ |
| 24 | 带符号扫穿档深(每单市价单吃穿档数的紧迫度) Signed sweep-depth urge | 成交流 | 5 | 4 | F6>0(买侧比卖侧吃book更狠)→次 | ★ |
| 25 | 可观测知情成交占比(PIN式交集,因果版) Observable informed-trad | 成交流 | 5 | 4 | F7>0(净知情买主导)→次日正收益。 | ○ |
| 26 | 主动流的量加权区间位置(订单流的带符号收盘位置值) Volume-weighted rang | 成交流 | 4 | 4 | F8>0(主动买居区间高位、卖居低位=强 | ★ |
| 27 | 冲击后深度恢复不对称 DRA Depth Recovery Asymmetry | 队列弹性 | 5 | 4 | 正(+): 买盘弹性强于卖盘 -> 隐性 | ★ |
| 28 | 3秒内闪现流动性不平衡 FLI Flash Liquidity Imbalance (sna | 队列弹性 | 5 | 4 | 正(+): 买侧闪现被成交占优 -> 隐 | ★ |
| 29 | 逐档单笔挂量形状与前排集中度 GDS Granularity Depth Shape | 队列弹性 | 4 | 5 | 正(+): 买侧前排单笔更大/更集中 - | ★ |
| 30 | 队列前排vs后排消耗-补充速度差 QVA Queue Velocity Asymmetry | 队列弹性 | 5 | 4 | 正(+): 买侧'前消后补'快于卖侧 - | ★ |
| 31 | 补单追价vs守价倾向 RAP Replenishment Aggressiveness | 队列弹性 | 4 | 4 | 正(+): 买方被打后追价补单强于卖方  | ★ |
| 32 | 深度流失的撤单vs成交归因(脆弱度)DFR Depth Fragility Ratio | 队列弹性 | 4 | 5 | 正(+): 买盘稳(成交主导)而卖盘脆( | ★ |
| 33 | 深度对净委托流的回补弹性系数 REL Resilience Elasticity | 队列弹性 | 5 | 4 | 正(+): 卖压来袭买盘深度更能维持/回 | ★ |
| 34 | 对数深度衰减弹性的买卖不对称 Log-Depth Decay Elasticity Asym | 盘口几何 | 4 | 4 | 正(X 越大买侧供给越有弹性 → 前向收 | ★ |
| 35 | 深度集中度(Herfindahl/熵)的买卖不对称 Depth Concentration  | 盘口几何 | 4 | 4 | 正(X>0 买侧集中于最优档 → 短期急 | ○ |
| 36 | 累积深度洛伦兹曲率(前置/后置流动性) Cumulative Depth Lorenz Co | 盘口几何 | 5 | 4 | 正(X>0 买侧流动性更贴近最优价 →  | ★ |
| 37 | 走簿冲击成本曲线的买卖不对称与凸性 Walk-the-Book Impact Asymmet | 盘口几何 | 4 | 4 | 正(AI>0 买入相对更贵即买侧承接强于 | ★ |
| 38 | 多档微价格期限结构斜率 Multi-Level Microprice Term-Struct | 盘口几何 | 4 | 4 | 正(X>0 深档微价格上移 → 深层买压 | ○ |
| 39 | 逐笔委托整数价位聚集的买卖不对称 Order-Price Round-Number Clus | 盘口几何 | 5 | 4 | 负(RC_b 偏高=散户情绪化买盘聚集= | ★ |
| 40 | 逐档平均单笔挂量的斜率与最优档块度 Per-Level Avg Order Size Slo | 盘口几何 | 4 | 4 | 正(X>0 买侧最优档大单坐镇 → 机构 | ★ |
| 41 | 买卖侧深度形状的有向分布距离 Signed Book-Shape Wasserstein D | 盘口几何 | 5 | 4 | 正(X>0 买侧形状更贴近 mid →  | ★ |
| 42 | 深度质心/有效盘口宽度的买卖不对称 Depth Center-of-Mass Width A | 盘口几何 | 3 | 5 | 正(X>0 买紧卖远 → 前向收益偏正) | ○ |
| 43 | 盘口 tick 跳空缺口度的买卖不对称 Book Tick-Gap Sparsity Asy | 盘口几何 | 4 | 4 | 负(X>0 买侧更空/下方无承接 → 前 | ○ |
| 44 | 深度单调性破坏(隐藏墙)有向强度 Depth Monotonicity-Violation  | 盘口几何 | 5 | 4 | 正(X>0 下方支撑墙占优 → 前向收益 | ★ |
| 45 | 封板脆弱指数 Limit-Up Seal Fragility via order lifec | 制度 | 5 | 4 | Negative: higher can | ★ |
| 46 | 开板振荡强度与二次封板资金比 Board-opening oscillation & re- | 制度 | 4 | 3 | OpenCount, OpenDurat | ★ |
| 47 | 临停前排队净方向与逼近速度 Pre-limit queue net direction &  | 制度 | 4 | 3 | Positive: positive q | ★ |
| 48 | 开盘竞价不可撤区失衡跃变 Opening-auction pre-vs-post 09:20 | 制度 | 4 | 2 | Positive CommittedIm | ★ |
| 49 | 收盘集合竞价机构调仓足迹 Closing-auction institutional sin | 制度 | 5 | 4 | Positive net institu | ★ |
| 50 | T+1 卖压来源结构因子 T+1 sell-supply provenance asymme | 制度 | 5 | 4 | Negative: high persi | ★ |
| 51 | tick 绑定流动性压缩因子 Tick-constrained pinned-spread  | 制度 | 4 | 3 | Positive: positive p | ★ |
| 52 | 涨跌停附近隐藏/冰山买盘 Hidden/iceberg buying near the li | 制度 | 3 | 2 | Positive: high hidde | ★ |
| 53 | 跌停排队卖单净方向与撤单弃卖 Limit-down queue net direction  | 制度 | 5 | 4 | Growing wall + faili | ★ |
| 54 | 大单挂单存活时长买卖不对称 Large-order resting-lifetime asy | 制度 | 4 | 3 | Positive: patient co | ★ |
| 55 | 特质主动流 Alpha（对市场+行业流残差化） Idiosyncratic Taker-Fl | 横截面 | 4 | 4 | 正: 累积特质净买入越大, 1日前向收益 | ★ |
| 56 | 订单流共动性 R²（拥挤/羊群显性度量） Order-Flow Commonality R- | 横截面 | 4 | 3 | 负: R²越高(越拥挤/被动)->1日前 | ★ |
| 57 | 订单流引领度（信息领先者） Order-Flow Leadership | 横截面 | 4 | 3 | 正: 领先且净买->1日收益高; 领先且 | ★ |
| 58 | 跟随者收敛缺口（相对欠反应） Laggard Convergence Gap | 横截面 | 4 | 3 | 正: 欠反应缺口越大->1日收益越高(向 | ★ |
| 59 | 激进买入拥挤集中度 Aggressive-Buy Crowding Concentratio | 横截面 | 3 | 3 | 负: 截面拥挤度越高->1日前向收益越低 | ★ |
| 60 | 相对冲击成本分位及其改善 Relative Impact-Cost Percentile a | 横截面 | 3 | 4 | F6a正(相对越不流动->流动性溢价-> | ★ |
| 61 | 相对队列存活/撤单不对称 Relative Queue-Life and Cancel As | 横截面 | 5 | 4 | 正: 相对同行买盘更坚定(撤少、存活久) | ★ |
| 62 | 特质流确认的特质动量 Flow-Confirmed Idiosyncratic Moment | 横截面 | 4 | 4 | 正: 特质涨且流确认->延续(正); 背 | ★ |
| 63 | 对市场流冲击的相对反应速度 Relative Response-Speed to Marke | 横截面 | 4 | 3 | 正: 慢反应且近端市场净买(CM>0)- | ★ |
| 64 | 相对深度韧性（顶档回补速度对同业残差） Relative Depth Resilience  | 横截面 | 4 | 3 | 正: 相对同行买侧顶档回补越快(隐性买盘 | ★ |
| 65 | 签名订单流路径效率(有符号) Signed Order-Flow Path Efficien | 日内路径 | 5 | 4 | positive (efficient  | ★ |
| 66 | 价流路径领先-衰竭度 Price-Flow Lead / Exhaustion | 日内路径 | 5 | 3 | positive (flow leads | ★ |
| 67 | 签名订单流加速度(后-前段斜率差) Signed Order-Flow Accelerati | 日内路径 | 4 | 3 | positive (accelerati | ★ |
| 68 | 签名订单流持续性(量加权自相关) Signed Order-Flow Persistence | 日内路径 | 4 | 3 | positive (persistent | ★ |
| 69 | 日内VWAP相对价路径位置与漂移 Intraday VWAP-Relative Price  | 日内路径 | 3 | 4 | positive (price pers | ★ |
| 70 | 开盘30分钟流锚定与确认外推 Opening 30-min Flow Anchor & Co | 日内路径 | 4 | 4 | positive (opening ne | ★ |
| 71 | 大单成交-撤单生命周期诚意失衡 Large-Order Fill-vs-Cancel Lif | 日内路径 | 4 | 3 | positive (bids since | ★ |

★ = 经对抗评审 / 推荐；○ = 同族备选（机制与推荐项重叠，实测择一）。

---

## 4. 如何保证边际贡献（正交化 / 增量 alpha 评估）

**(1) 事前机制正交**：上表每个因子已刻意避开 OBI/Amihud/动量/裸 VPIN/主力净流入等基线，机制上多为「生命周期化 / 几何化 / 横截面化 / 条件化」重构——这是边际增量的第一道保证。

**(2) 事后统计正交化（必做）**：在每个 30-min 时点做横截面回归取残差：

```
factor_new_resid = factor_new  ~  1 + ln(流通市值) + ln(截至t换手) + 已实现波动
                                  + 反转(过去5日收益) + 平均价差
                                  + [你库里最相关的 K 个已有因子]
                   -> 取残差 -> winsorize(1/99) -> 截面 z-score -> 行业中性(申万一级)
```

只用残差进入组合/评估——度量的是**已有库解释不掉的部分**。

**(3) 增量评估三件套**（用本仓库工具跑）：
- **RankIC / IC 衰减**：对 `stats/mid` 生成的 mid 标签算 1 日前向收益（`compute_label`），用 `ic_calculator`（横截面用 `cs_ic_calculator`）算残差因子的 RankIC 与 t 值。看 |IC|、ICIR、IC 胜率。
- **正交后增量 IC**：`ΔIC = IC(已有库∪新因子) − IC(已有库)`，以及新因子加入 Barra 式多因子回归后的 t 值/增量 ICIR。这是「边际贡献」的硬指标。
- **分组多空 + 换手**：`edge_calculator`（`cs_edge_calculator`）看 top/bottom 分位前向收益差与 Edge；同时算日频换手率——低频因子换手应低，否则被交易成本吃掉边际 alpha。

**(4) 组合层面**：对存活的新因子做相关性聚类（|corr|>0.7 归一族，族内保留 ICIR 最高者），再对已有库做逐步回归/正交增量筛选，最终只保留能提升组合 ICIR / 降低回撤的少数几个。

---

## 5. 落地工程路径（结合 wolverine-demo / wlsim）

1. **数据接入**：横截面模式，配 `cs-snapshot`（levels: 10，fields 至少 `bp/bv/ap/av/bid_cnt/ask_cnt/last_price/volume/turnover`）+ `cs-mbo`。C++ 用 `CsSnapshotUtils::get_fld<FldType::...>` 与 `CsMboUtils::get_fld<TradeFldType::...>` 取字段。
2. **关键前提确认（重要）**：本仓库 `CsMboEvent` 只暴露**成交流** `TradeFldType={Cnt,Price,Qty,Side,BidId,AskId}`。约一半因子（视角一全组、FLI/QVA、OPRCA、DMVHW 真伪加权等）需要**逐笔委托 add / 撤单**事件字段。**上线前必须向引擎/数据团队确认 cs-mbo 是否暴露 Order-add / Cancel 的 order_id/side/price/qty**；若暂不可得，先做「纯成交流 + 快照」子集（视角三/五/八大部分、DRA/DFR/REL、成交流组、几何组中不依赖 MBO 真伪加权者），委托生命周期组待字段就绪再上。
3. **时间基准**：日内门控/时长一律用 `exchtime`（当日纳秒），跨日/日志才用 `localtime`（epoch），严禁混用。8 个时点判定复用 `stats/mid` 的 `time::hhmmss_to_exchtime` + sorted targets + cursor 写法。
4. **无 look-ahead**：所有「截至 t」累积严格 `exchtime<=t`；用到冲击后窗/恢复窗/撤单时 best 的因子，其窗口必须整体落在 t 之前，或用前一事件快照。
5. **制度过滤**：每日剔除触涨跌停/停牌/单边簿样本；集合竞价段（09:15–09:25、14:57–15:00）单独处理或排除；ST/双创用对应 cap（±5%/±20%）。
6. **跨日状态**：需要「近 20 日 ADV / 滚动分位 q90」等的因子，用 `save_state/load_state`（zpp_bits）或 cfi_operators 的 `save_checkpoint` 跨日携带，避免每日冷启动。
7. **输出与评估**：每标的一个值，`apis_.update_signal(token, exchtime, localtime, ins_nr, sigs)`；`output.module: parquet/csv`，落盘后进入第 4 节评估闭环（`compute_label` -> `ic_calculator`/`edge_calculator`）。

---

## 6. Top Picks（最高把握 + 最新颖，建议优先实现）

| 优先级 | 因子 | 为什么先做 |
|---|---|---|
| 1 | **DRA + REL**（队列弹性） | 只需成交流+快照即可算（不卡 MBO 委托字段），机制硬（冲击后承接），nov5/lf4，最快能出 IC |
| 2 | **IdioFlow + FlowConfirmedMom**（横截面特质） | 天生正交于已有库（已剔除市场/行业共动），低频稳定，边际 alpha 概率最高 |
| 3 | **收盘竞价机构足迹 + T+1 卖压结构**（制度） | A 股制度独有、前人极少做、直指次日漂移，lf4，机构足迹信息量大 |
| 4 | **ToxicNetTurnover + SignedSweepDepth**（成交流） | 用 BidId/AskId 聚簇（本仓库已暴露），把「大单净流入」升级为扫穿×逆选择加权，正交且可算 |
| 5 | **SW-DOBI + RelQueueLife**（生命周期，待委托字段） | 确认 Order/Cancel 字段可得后，委托生命周期里 ICIR 潜力最高，把 OBI/撤单比彻底重构 |
| 6 | **CDLC + SBSWD**（几何） | 纯快照可算、低换手，用洛伦兹/最优传输度量盘口形状，与全库高度正交，适合稳定底仓因子 |

---

## 附录 A：数据模型

- **频率与形态**：每 3s 一个 cross-sectional 事件，全 A 股 universe 所有标的同时推送；一个事件里既有该 3s 窗口内的 snapshot，也有该 3s 窗口内累积的全部逐笔 MBO。
- **Snapshot（10 档）**：`bp[0..9]/bv[0..9]`、`ap[0..9]/av[0..9]`、`bid_cnt[level]/ask_cnt[level]`（每档挂单笔数，可推每档平均单笔挂量 = `bv[l]/bid_cnt[l]`）、`last_price`、`volume`（当日累计）、`turnover`（当日累计）、`total_bid_vol/total_ask_vol/total_bid_cnt/total_ask_cnt/total_bid_qty/total_ask_qty`、`exchtime`（当日纳秒）、`localtime`（epoch 纳秒）。
- **MBO（3s 窗口内全部逐笔，带委托号）**：Order 逐笔委托(add)：`order_id, side, price, qty, 委托类型`；Cancel 逐笔撤单：`被撤 order_id, side, price, qty`；Trade 逐笔成交：`price, qty, 主动方 side, bid_id, ask_id`。用 `order_id`/`bid_id`/`ask_id` 可做 Order↔Trade↔Cancel 生命周期匹配、重建限价簿队列与队列位置。
- **本仓库已暴露**：`CsMboEvent` 成交流字段 `TradeFldType={Cnt,Price,Qty,Side,BidId,AskId}`。逐笔委托/撤单字段用户数据源里存在，但需向引擎确认暴露方式（见第 5 节第 2 点）。

## 附录 B：A 股制度约束

- **T+1**：当日买入不可当日卖出 → 买卖紧迫性不对称、日内无法平多头。
- **涨跌停**：±10%（ST ±5%；科创板/创业板 ±20%），触及后单边排队（封板/开板）动态。
- **集合竞价**：开盘 09:15–09:25（09:20–09:25 不可撤单）、收盘 14:57–15:00；连续竞价 09:30–11:30 / 13:00–15:00。
- **tick = 0.01 元**；买入最小 100 股；多数股票禁止日内融券做空（卖压主要来自持仓者）。

## 附录 C：生成过程说明（诚实汇报）

本报告由多智能体 fan-out 生成：8 个微结构视角各自「生成 ≥9 个因子 -> 对抗式评审精选」。两次 workflow 运行中，部分 agent 因结构化输出超时/校验失败；最终 8 个视角全部补齐——5 个视角经对抗式评审精选（标 ★），3 个视角（委托生命周期、知情成交流、盘口几何）由生成候选经人工去重精选（★=推荐，○=同族备选，未经自动对抗评审，落地前建议再自查公式与 look-ahead）。所有因子均标注可算性与陷阱；依赖逐笔委托/撤单字段者，请先做第 5 节第 2 点的数据字段确认再上线。
