"""Extract exact current NGene functions for an isolated audit executable.

Only environment services are stubbed in audit-pvt-cases.c. No third-party
receiver code is used. Generated source stays in build/ and is never production.
"""
import argparse
import hashlib
import json
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--kalman-source', type=pathlib.Path,
                    help='Explicit archived kalmanFilter.c for a baseline comparison')
args = parser.parse_args()
out = root / 'build' / 'icd-audit'
out.mkdir(parents=True, exist_ok=True)
selection = {
    'pvt/pvt.c': ['ResetPvt', 'UpdatePvtMinimumChannels', 'ResetPseudorangeOrigin', 'DeleteOperativeChannel', 'ComputeTransmissionTime',
                  'VerifyTransmissionTime', 'ComputePseudoranges', 'ComputeSatsPositions',
                  'DeleteNotSmoothed', 'ApplyElevationMask'],
    'pvt/pvtUtility.c': ['CheckT', 'ComputeAzEl', 'Tropo', 'TropoHopfieldModel',
                         'Topocent', 'Ecef2Geod', 'EarthCorrection', 'RotateSatellitePosition'],
    'utilityGnss/timeConversions.c': ['GetGstToGpsTimeOffset', 'Sign', 'Module', 'FromGnssTimeToUtcTime'],
    'pvt/receiverTime.c': ['SetReferenceRxTime', 'PredictRxTime'],
    'pvt/kalmanFilter.c': ['ResetKalmanMotionData', 'ApplyKalmanMotionNoise', 'AdaptKalmanMotion',
                          'InitKalmanFilterPhi', 'InitKalmanFilterPostCovarianceMatrices',
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
chunks.insert(0, '\n'.join(re.findall(r'^#define KF_MOTION_[^\n]*',
    (root / 'pvt/kalmanFilter.c').read_text(encoding='utf-8'), re.M)) + '\n')
manifest = {}
for filename, names in selection.items():
    selected = args.kalman_source if filename == 'pvt/kalmanFilter.c' and args.kalman_source else root / filename
    raw = selected.read_bytes()
    source = raw.decode('utf-8-sig')
    manifest[filename] = {'sha256': hashlib.sha256(raw).hexdigest(), 'functions': names}
    if selected != root / filename:
        manifest[filename]['baseline_source'] = str(selected.resolve())
    for name in names:
        function_source = source
        match = re.search(r'^\w[^\n;]*\b' + name + r'\([^;]*?\)\s*\{', function_source, re.M)
        if not match and args.kalman_source and filename == 'pvt/kalmanFilter.c' and name in {
                'ResetKalmanMotionData', 'AdaptKalmanMotion', 'ApplyKalmanMotionNoise'}:
            # Older baseline filters do not call the adaptive helpers. Keep the
            # current reset/harness linkable and record this supplementation.
            function_source = (root / filename).read_text(encoding='utf-8')
            match = re.search(r'^\w[^\n;]*\b' + name + r'\([^;]*?\)\s*\{', function_source, re.M)
            manifest[filename].setdefault('supplemental_functions', {})[name] = hashlib.sha256(
                (root / filename).read_bytes()).hexdigest()
        if not match:
            raise ValueError(f'Cannot find definition {filename}:{name}')
        start = match.start()
        # Strip comments and strings only for brace matching; offsets stay intact.
        tail = function_source[start:]
        clean = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                       lambda m: ' ' * len(m.group()), tail, flags=re.S)
        depth = 0
        for i, char in enumerate(clean):
            if char == '{':
                depth += 1
            elif char == '}':
                depth -= 1
                if depth == 0:
                    chunks.append(f'\n#line {function_source[:start].count(chr(10))+1} "{filename}"\n' + tail[:i+1] + '\n')
                    break
        else:
            raise ValueError(f'Unbalanced function {name}')
for header in ['pvt/pntNumerics.h', 'pvt/pntNumerics.cpp', 'pvt/pvtConstants.h',
               'pvt/pvtDataStructure.h', 'pvt/pvtChecks.h', 'utilityGnss/gnssTimeMath.h',
               'utilityGnss/navigationMessageParser.h', 'utilityGnss/gpsMessageParser.h',
               'development/profiling/audit-pvt-cases.c',
               'development/profiling/extract-pvt-audit.py']:
    manifest[header] = {'sha256': hashlib.sha256((root / header).read_bytes()).hexdigest(), 'functions': []}
(out / 'ngene-audit-extracted.c').write_text(''.join(chunks), encoding='utf-8')
(out / 'source-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('Exact NGene functions extracted:', sum(map(len, selection.values())))
