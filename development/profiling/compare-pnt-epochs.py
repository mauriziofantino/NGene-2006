"""Compare ENU solution CSVs only at common receive epochs and satellite counts.

Counts do not establish satellite identity. Input is analyze-gnss-log.ps1 output.
This tool is independent of the production build and never tunes receiver data.
"""
import argparse
import csv
import json
import math
from pathlib import Path


def load(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        return {int(row['RxMs']): row for row in csv.DictReader(stream)
                if int(row['GpsSatellites']) + int(row['GalileoSatellites']) > 0}


def stats(rows):
    return {'Count': len(rows), **{
        name: math.sqrt(sum(float(row[column])**2 for row in rows)/len(rows))
        if rows else None for name, column in [('HorizontalRms','Horizontal'),('UpRms','Up')]}}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before');parser.add_argument('after');parser.add_argument('output')
    args=parser.parse_args()
    before,after=load(args.before),load(args.after)
    common=sorted(before.keys() & after.keys())
    matched=[epoch for epoch in common if all(before[epoch][field]==after[epoch][field]
        for field in ('GpsSatellites','GalileoSatellites'))]
    result={'Before':str(args.before),'After':str(args.after),'CommonEpochs':len(common),
        'SameCountsEpochs':matched,'BeforeStatistics':stats([before[t] for t in matched]),
        'AfterStatistics':stats([after[t] for t in matched]),
        'Limit':'Same epoch and constellation counts; PRN identities are not encoded in this CSV.'}
    Path(args.output).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
