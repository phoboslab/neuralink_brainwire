This is an attempt at the [Neuralink Compression Challenge](https://content.neuralink.com/compression-challenge/README.html). It meets almost all target criteria:

- lossless: reproduces input data exactly
- no algorithmic delay: samples can be immediately encoded and sent
- simple enough to run on a gameboy

It _just_ misses the compression target (200x) by two orders of magnitude: the compression rate of this solution is 3.39x for the provided data set.

Output from `eval.sh`:

```
All recordings successfully compressed.
Original size (bytes): 146800526
Compressed size (bytes): 43200926
Compression ratio: 3.39
```

Compression is done by:

1. set `previous_sample = 0`, `rice_k = 3`
2. take the difference of `previous_sample` and current `sample`
3. predict the difference(!) using an LMS
4. encode the difference to the predicted difference using [rice coding](https://en.wikipedia.org/wiki/Golomb_coding#Rice_coding)
5. estimate next `rice_k` based on bit length of this encode
6. go to 2.
