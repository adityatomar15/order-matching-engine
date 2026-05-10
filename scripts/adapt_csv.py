#!/usr/bin/env python3
"""Map Binance prices into 9500‑10500 range for engine compatibility."""
import csv, sys

def main(input_path, output_path):
    with open(input_path, 'r') as fin, open(output_path, 'w', newline='') as fout:
        reader = csv.reader(fin)
        writer = csv.writer(fout)
        header = next(reader)
        writer.writerow(header)
        for row in reader:
            if row[1] == 'A':           # Add events only
                try:
                    price = float(row[3])
                    mapped = 9500 + (int(price) % 1001)
                    row[3] = str(mapped)
                    writer.writerow(row)
                except (ValueError, IndexError):
                    pass

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: adapt_csv.py <input.csv> <output.csv>")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
