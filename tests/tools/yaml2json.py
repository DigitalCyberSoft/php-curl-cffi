#!/usr/bin/env python3
"""Convert upstream YAML signature files to JSON for PHP test suite."""
import sys, json, yaml, os, glob

class BytesEncoder(json.JSONEncoder):
    """Handle YAML binary tags by encoding bytes as hex strings."""
    def default(self, obj):
        if isinstance(obj, bytes):
            return obj.hex()
        return super().default(obj)

def convert(yaml_path, json_path):
    with open(yaml_path) as f:
        data = yaml.safe_load(f)
    with open(json_path, 'w') as f:
        json.dump(data, f, indent=2, cls=BytesEncoder)

if __name__ == '__main__':
    src = sys.argv[1] if len(sys.argv) > 1 else 'curl-impersonate/tests/signatures'
    dst = sys.argv[2] if len(sys.argv) > 2 else 'tests/signatures'
    os.makedirs(dst, exist_ok=True)
    for yf in sorted(glob.glob(os.path.join(src, '*.yaml'))):
        name = os.path.splitext(os.path.basename(yf))[0]
        jf = os.path.join(dst, name + '.json')
        convert(yf, jf)
        print(f'{name}.yaml -> {name}.json')
