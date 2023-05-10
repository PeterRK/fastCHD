# fastCHD
An implement of [CHD algorithm](http://cmph.sourceforge.net/chd.html), optimized for modern platform. It can provide sub-billion level QPS on single machine.

![](throughput.png)

### Key Features
* extreme low space overhead (4.3 bits per item)
* amazing read performance
* fast build with extreme low false failure rate
* no online writing
* work on CPU support little-endian unaligned memory access (X86，ARM，RISC-V...)

### Other Solutions
* [faster reading](https://github.com/PeterRK/SSHT)
* [online writable](https://github.com/PeterRK/estuary)

---
[【Chinese】](README-CN.md) [【English】](README.md)
