*** Booting Zephyr OS build openbmc-zephyr-v0.1.0-509-g18db2356fdd3 ***
==========================================
wolfSSL AES Throughput Test (GCM + ECB)
==========================================
AES path: Linkedsemi hardware

========== AES-128-GCM ==========

--- AES-128-GCM correctness check ---
[GCM-prof] #1  call: CTR(hw)=2181  GHASH(sw)=4842 cyc | run: CTR=2181 GHASH=4842 cyc  (CTR 31% / GHASH 68%)
  len=    16: encrypt+decrypt OK
[GCM-prof] #2  call: CTR(hw)=3750  GHASH(sw)=7107 cyc | run: CTR=5931 GHASH=11949 cyc  (CTR 33% / GHASH 66%)
  len=    64: encrypt+decrypt OK
[GCM-prof] #3  call: CTR(hw)=1190160  GHASH(sw)=1851348 cyc | run: CTR=1196091 GHASH=1863297 cyc  (CTR 39% / GHASH 60%)
  len= 32768: encrypt+decrypt OK
AES-128-GCM correctness check PASSED
[GCM-prof] #4  call: CTR(hw)=1190116  GHASH(sw)=1851507 cyc | run: CTR=2386207 GHASH=3714804 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #5  call: CTR(hw)=1186086  GHASH(sw)=1851424 cyc | run: CTR=3572293 GHASH=5566228 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #6  call: CTR(hw)=1186058  GHASH(sw)=1851452 cyc | run: CTR=4758351 GHASH=7417680 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #7  call: CTR(hw)=1186082  GHASH(sw)=1851452 cyc | run: CTR=5944433 GHASH=9269132 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #8  call: CTR(hw)=1186082  GHASH(sw)=1851452 cyc | run: CTR=7130515 GHASH=11120584 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #9  call: CTR(hw)=1186082  GHASH(sw)=1851452 cyc | run: CTR=8316597 GHASH=12972036 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #10  call: CTR(hw)=1186082  GHASH(sw)=1851452 cyc | run: CTR=9502679 GHASH=14823488 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #11  call: CTR(hw)=1186082  GHASH(sw)=1851452 cyc | run: CTR=10688761 GHASH=16674940 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #12  call: CTR(hw)=1186082  GHASH(sw)=1851452 cyc | run: CTR=11874843 GHASH=18526392 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #13  call: CTR(hw)=1186082  GHASH(sw)=1851452 cyc | run: CTR=13060925 GHASH=20377844 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #14  call: CTR(hw)=1186082  GHASH(sw)=1851452 cyc | run: CTR=14247007 GHASH=22229296 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #15  call: CTR(hw)=1186082  GHASH(sw)=1851452 cyc | run: CTR=15433089 GHASH=24080748 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #16  call: CTR(hw)=593186  GHASH(sw)=929340 cyc | run: CTR=16026275 GHASH=25010088 cyc  (CTR 39% / GHASH 60%)
--------------------------------------------
AES-128-GCM encrypt throughput
  Total data  : 409600 bytes (400 KB)
  Total cycles: 109911482
  Total time  : 183185 us (183 ms)
  Throughput  : 2183 KB/s (2 MB/s)
--------------------------------------------
[GCM-prof] #17  call: CTR(hw)=1186246  GHASH(sw)=1851382 cyc | run: CTR=17212521 GHASH=26861470 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #18  call: CTR(hw)=1185183  GHASH(sw)=1851010 cyc | run: CTR=18397704 GHASH=28712480 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #19  call: CTR(hw)=1185183  GHASH(sw)=1851010 cyc | run: CTR=19582887 GHASH=30563490 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #20  call: CTR(hw)=1185183  GHASH(sw)=1850994 cyc | run: CTR=20768070 GHASH=32414484 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #21  call: CTR(hw)=1185183  GHASH(sw)=1850994 cyc | run: CTR=21953253 GHASH=34265478 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #22  call: CTR(hw)=1185183  GHASH(sw)=1850994 cyc | run: CTR=23138436 GHASH=36116472 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #23  call: CTR(hw)=1185183  GHASH(sw)=1850994 cyc | run: CTR=24323619 GHASH=37967466 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #24  call: CTR(hw)=1185183  GHASH(sw)=1850994 cyc | run: CTR=25508802 GHASH=39818460 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #25  call: CTR(hw)=1185183  GHASH(sw)=1850994 cyc | run: CTR=26693985 GHASH=41669454 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #26  call: CTR(hw)=1185183  GHASH(sw)=1850994 cyc | run: CTR=27879168 GHASH=43520448 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #27  call: CTR(hw)=1185183  GHASH(sw)=1850994 cyc | run: CTR=29064351 GHASH=45371442 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #28  call: CTR(hw)=1185183  GHASH(sw)=1850994 cyc | run: CTR=30249534 GHASH=47222436 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #29  call: CTR(hw)=592287  GHASH(sw)=928882 cyc | run: CTR=30841821 GHASH=48151318 cyc  (CTR 39% / GHASH 60%)
--------------------------------------------
AES-128-GCM decrypt throughput
  Total data  : 409600 bytes (400 KB)
  Total cycles: 37946510
  Total time  : 63244 us (63 ms)
  Throughput  : 6324 KB/s (6 MB/s)
--------------------------------------------

========== AES-256-GCM ==========

--- AES-256-GCM correctness check ---
[GCM-prof] #30  call: CTR(hw)=2292  GHASH(sw)=5011 cyc | run: CTR=30844113 GHASH=48156329 cyc  (CTR 39% / GHASH 60%)
  len=    16: encrypt+decrypt OK
[GCM-prof] #31  call: CTR(hw)=4364  GHASH(sw)=7513 cyc | run: CTR=30848477 GHASH=48163842 cyc  (CTR 39% / GHASH 60%)
  len=    64: encrypt+decrypt OK
[GCM-prof] #32  call: CTR(hw)=1485182  GHASH(sw)=1851607 cyc | run: CTR=32333659 GHASH=50015449 cyc  (CTR 39% / GHASH 60%)
  len= 32768: encrypt+decrypt OK
AES-256-GCM correctness check PASSED
[GCM-prof] #33  call: CTR(hw)=1485154  GHASH(sw)=1851611 cyc | run: CTR=33818813 GHASH=51867060 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #34  call: CTR(hw)=1481142  GHASH(sw)=1851612 cyc | run: CTR=35299955 GHASH=53718672 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #35  call: CTR(hw)=1481136  GHASH(sw)=1851604 cyc | run: CTR=36781091 GHASH=55570276 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #36  call: CTR(hw)=1481136  GHASH(sw)=1851610 cyc | run: CTR=38262227 GHASH=57421886 cyc  (CTR 39% / GHASH 60%)
[GCM-prof] #37  call: CTR(hw)=1481136  GHASH(sw)=1851610 cyc | run: CTR=39743363 GHASH=59273496 cyc  (CTR 40% / GHASH 59%)
[GCM-prof] #38  call: CTR(hw)=1481136  GHASH(sw)=1851610 cyc | run: CTR=41224499 GHASH=61125106 cyc  (CTR 40% / GHASH 59%)
[GCM-prof] #39  call: CTR(hw)=1481136  GHASH(sw)=1851610 cyc | run: CTR=42705635 GHASH=62976716 cyc  (CTR 40% / GHASH 59%)
[GCM-prof] #40  call: CTR(hw)=1481136  GHASH(sw)=1851610 cyc | run: CTR=44186771 GHASH=64828326 cyc  (CTR 40% / GHASH 59%)
[GCM-prof] #41  call: CTR(hw)=1481136  GHASH(sw)=1851610 cyc | run: CTR=45667907 GHASH=66679936 cyc  (CTR 40% / GHASH 59%)
[GCM-prof] #42  call: CTR(hw)=1481136  GHASH(sw)=1851610 cyc | run: CTR=47149043 GHASH=68531546 cyc  (CTR 40% / GHASH 59%)
[GCM-prof] #43  call: CTR(hw)=1481136  GHASH(sw)=1851610 cyc | run: CTR=48630179 GHASH=70383156 cyc  (CTR 40% / GHASH 59%)
[GCM-prof] #44  call: CTR(hw)=1481136  GHASH(sw)=1851610 cyc | run: CTR=50111315 GHASH=72234766 cyc  (CTR 40% / GHASH 59%)
[GCM-prof] #45  call: CTR(hw)=740784  GHASH(sw)=929570 cyc | run: CTR=50852099 GHASH=73164336 cyc  (CTR 41% / GHASH 58%)
--------------------------------------------
AES-256-GCM encrypt throughput
  Total data  : 409600 bytes (400 KB)
  Total cycles: 114492610
  Total time  : 190821 us (190 ms)
  Throughput  : 2096 KB/s (2 MB/s)
--------------------------------------------
[GCM-prof] #46  call: CTR(hw)=1481330  GHASH(sw)=1851530 cyc | run: CTR=52333429 GHASH=75015866 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #47  call: CTR(hw)=1480231  GHASH(sw)=1851104 cyc | run: CTR=53813660 GHASH=76866970 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #48  call: CTR(hw)=1480293  GHASH(sw)=1851110 cyc | run: CTR=55293953 GHASH=78718080 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #49  call: CTR(hw)=1480259  GHASH(sw)=1851104 cyc | run: CTR=56774212 GHASH=80569184 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #50  call: CTR(hw)=1480293  GHASH(sw)=1851110 cyc | run: CTR=58254505 GHASH=82420294 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #51  call: CTR(hw)=1480259  GHASH(sw)=1851104 cyc | run: CTR=59734764 GHASH=84271398 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #52  call: CTR(hw)=1480293  GHASH(sw)=1851110 cyc | run: CTR=61215057 GHASH=86122508 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #53  call: CTR(hw)=1480259  GHASH(sw)=1851104 cyc | run: CTR=62695316 GHASH=87973612 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #54  call: CTR(hw)=1480293  GHASH(sw)=1851110 cyc | run: CTR=64175609 GHASH=89824722 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #55  call: CTR(hw)=1480259  GHASH(sw)=1851104 cyc | run: CTR=65655868 GHASH=91675826 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #56  call: CTR(hw)=1480293  GHASH(sw)=1851110 cyc | run: CTR=67136161 GHASH=93526936 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #57  call: CTR(hw)=1480259  GHASH(sw)=1851104 cyc | run: CTR=68616420 GHASH=95378040 cyc  (CTR 41% / GHASH 58%)
[GCM-prof] #58  call: CTR(hw)=739941  GHASH(sw)=929070 cyc | run: CTR=69356361 GHASH=96307110 cyc  (CTR 41% / GHASH 58%)
--------------------------------------------
AES-256-GCM decrypt throughput
  Total data  : 409600 bytes (400 KB)
  Total cycles: 41637776
  Total time  : 69396 us (69 ms)
  Throughput  : 5764 KB/s (5 MB/s)
--------------------------------------------

========== AES-128-ECB ==========

--- AES-128-ECB correctness check ---
  len=    16: ECB encrypt+decrypt OK
  len=    64: ECB encrypt+decrypt OK
  len= 32768: ECB encrypt+decrypt OK
AES-128-ECB correctness check PASSED
--------------------------------------------
AES-128-ECB encrypt throughput
  Total data  : 409600 bytes (400 KB)
  Total cycles: 12841916
  Total time  : 21403 us (21 ms)
  Throughput  : 18688 KB/s (18 MB/s)
--------------------------------------------
--------------------------------------------
AES-128-ECB decrypt throughput
  Total data  : 409600 bytes (400 KB)
  Total cycles: 17756876
  Total time  : 29594 us (29 ms)
  Throughput  : 13516 KB/s (13 MB/s)
--------------------------------------------

========== AES-256-ECB ==========

--- AES-256-ECB correctness check ---
  len=    16: ECB encrypt+decrypt OK
  len=    64: ECB encrypt+decrypt OK
  len= 32768: ECB encrypt+decrypt OK
AES-256-ECB correctness check PASSED
--------------------------------------------
AES-256-ECB encrypt throughput
  Total data  : 409600 bytes (400 KB)
  Total cycles: 16530856
  Total time  : 27551 us (27 ms)
  Throughput  : 14518 KB/s (14 MB/s)
--------------------------------------------
--------------------------------------------
AES-256-ECB decrypt throughput
  Total data  : 409600 bytes (400 KB)
  Total cycles: 28203302
  Total time  : 47005 us (47 ms)
  Throughput  : 8509 KB/s (8 MB/s)
--------------------------------------------

==========================================
All AES throughput tests PASSED
==========================================