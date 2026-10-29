"""Extract exact current NGene functions for an isolated audit executable.

Only environment services are stubbed in audit-pvt-cases.c. No third-party
receiver code is used. Generated source stays in build/ and is never production.
"""
import hashlib
import json
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
out = root / 'build' / 'icd-audit'
out.mkdir(parents=True, exist_ok=True)
selection = {
    'pvt/pvt.c': ['ResetPvt', 'UpdatePvtMinimumChannels', 'ResetPseudorangeOrigin', 'DeleteOperativeChannel', 'ComputeTransmissionTime',
                  'VerifyTransmissionTime', 'ComputePseudoranges', 'ComputeSatsPositions',
                  'DeleteNotSmoothed', 'ApplyElevationMask'],
    'pvt/pvtUtility.c': ['CheckT', 'ComputeAzEl', 'Tropo', 'TropoHopfieldModel',
                         'Topocent', 'Ecef2Geod', 'EarthCorrection', 'RotateSatellitePosition'],
    'utilityGnss/timeConversions.c': ['GetGstToGpsTimeOffset'],
    'pvt/kalmanFilter.c': ['InitKalmanFilterPhi', 'InitKalmanFilterPostCovarianceMatrices',
                          'InitQStateNoiseCovarianceMatrices', 'InitKalmanFilterData',
                          'ClearKalmanFilterData', 'DeinitKalmanFilterData',
                          'KalmanFilterPrediction', 'KalmanFilterRoutine',
                          'PreparePPost', 'UpdatePPostMulti', 'PerformKalmanFilter'],
    'pvt/carrierSmoothing.c': ['ClearCsmDataChannel', 'ClearCsmData', 'PerformCarrierSmoothing', 'UpdateCumulatedBiases'],
    'pvt/leastSquares.c': ['InitLeastSquaresData', 'ClearLeastSquaresData',
                         'DeinitLeastSquaresData', 'PerformLeastSquares'],
    'pvt/dop.c': ['DopEvaluation'],
    'utilityGnss/navigationMessageParser.c': ['ExtractField', 'ExtractTwoCompField'],
    'utilityGnss/gpsMessageParser.c': ['ParseGpsSubframe1', 'ParseGpsSubframe4'],
    'utilityGnss/galileoMessageParser.c': ['ParseGalWord01', 'ParseGalWord04', 'ParseGalWord05'],
    'pvt/pvtChecks.c': ['CheckPvtResultsGoodness'],
}
constant = re.search(r'^#define MINIMUM_CSM_ITERATION_BEFORE_USE[^\n]*',
                     (root / 'pvt/pvt.c').read_text(encoding='utf-8'), re.M).group()
chunks = [constant + '\n#include "audit-pvt-cases.c"\n']
chunks.insert(0, '\n'.join(re.findall(r'^#define MAXIMUM_[^\n]*',
    (root / 'pvt/pvtChecks.c').read_text(encoding='utf-8'), re.M)) + '\n')
manifest = {}
for filename, names in selection.items():
    raw = (root / filename).read_bytes()
    source = raw.decode('utf-8-sig')
    manifest[filename] = {'sha256': hashlib.sha256(raw).hexdigest(), 'functions': names}
    for name in names:
        match = re.search(r'^\w[^\n;]*\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
        if not match:
            raise ValueError(f'Cannot find definition {filename}:{name}')
        start = match.start()
        # Strip comments and strings only for brace matching; offsets stay intact.
        tail = source[start:]
        clean = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                       lambda m: ' ' * len(m.group()), tail, flags=re.S)
        depth = 0
        for i, char in enumerate(clean):
            if char == '{':
                depth += 1
            elif char == '}':
                depth -= 1
                if depth == 0:
                    chunks.append(f'\n#line {source[:start].count(chr(10))+1} "{filename}"\n' + tail[:i+1] + '\n')
                    break
        else:
            raise ValueError(f'Unbalanced function {name}')
for header in ['pvt/pntNumerics.h', 'pvt/pntNumerics.cpp', 'pvt/pvtConstants.h',
               'pvt/pvtDataStructure.h', 'pvt/pvtChecks.h', 'utilityGnss/gnssTimeMath.h',
               'utilityGnss/navigationMessageParser.h', 'utilityGnss/gpsMessageParser.h']:
    manifest[header] = {'sha256': hashlib.sha256((root / header).read_bytes()).hexdigest(), 'functions': []}
(out / 'ngene-audit-extracted.c').write_text(''.join(chunks), encoding='utf-8')
(out / 'source-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('Exact NGene functions extracted:', sum(map(len, selection.values())))
