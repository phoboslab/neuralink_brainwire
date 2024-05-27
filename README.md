This is an attempt at the [Neuralink Compression Challenge](https://content.neuralink.com/compression-challenge/README.html). It meets almost all target criteria:

- lossless: reproduces input data exactly
- no algorithmic delay: samples can be immediately encoded and sent
- simple enough to run on a gameboy

It _just_ misses the compression target (200x) by two orders of magnitude: the compression rate of this solution is 3.35x for the provided data set.

Output from `eval.sh`:

```
All recordings successfully compressed.
Original size (bytes): 146800526
Compressed size (bytes): 43774883
Compression ratio: 3.35
```

Compression is done by:

1. set `previous_sample = 0`, `rice_k = 3`
2. take the difference of `previous_sample` and current `sample`
3. encode the difference using [rice coding](https://en.wikipedia.org/wiki/Golomb_coding#Rice_coding)
4. estimate next `rice_k` based on bit length of this encode
5. go to 2.

I believe this is the "optimal" solution in terms of complexity and results. With a bit of prediction (similar to e.g. [qoa](https://github.com/phoboslab/qoa)) you can get to 3.4x. With entropy coding you _might_ get to 3.5x. In any case, what remains after the initial prediction (`residual = sample - previous_sample`) is very close to random noise, and noise famously compresses rather badly. I'd be very surprised if we see any solutions approaching (or even exceeding) 4x.

In conclusion, this challenge is either dishonest or ignorant.