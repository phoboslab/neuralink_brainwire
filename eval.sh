#!/usr/bin/env bash

# Modified to use the same binary for en-/decode

rm -rf data
unzip data.zip

get_file_size() {
  find "$1" -printf "%s\n"
}

total_size_raw=0
coder_size=$(get_file_size bwenc)
total_size_compressed=$((coder_size))

for file in data/*
do
  echo "Processing $file"
  compressed_file_path="${file}.bw"
  decompressed_file_path="${file}.dec.wav"

  ./bwenc "$file" "$compressed_file_path"
  ./bwenc "$compressed_file_path" "$decompressed_file_path"

  file_size=$(get_file_size "$file")
  compressed_size=$(get_file_size "$compressed_file_path")

  if diff -q "$file" "$decompressed_file_path" > /dev/null; then
      echo "${file} losslessly compressed from ${file_size} bytes to ${compressed_size} bytes"
  else
      echo "ERROR: ${file} and ${decompressed_file_path} are different."
      exit 1
  fi

  total_size_raw=$((total_size_raw + file_size))
  total_size_compressed=$((total_size_compressed + compressed_size))
done

compression_ratio=$(echo "scale=2; ${total_size_raw} / ${total_size_compressed}" | bc)

echo "All recordings successfully compressed."
echo "Original size (bytes): ${total_size_raw}"
echo "Compressed size (bytes): ${total_size_compressed}"
echo "Compression ratio: ${compression_ratio}"
