"""Single-shot read-only review CLI. Cancelling its QProcess cannot cancel training."""
import argparse
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend-root", required=True)
    parser.add_argument("--dataset", required=True)
    parser.add_argument("--group")
    parser.add_argument("--camera-dataset")
    parser.add_argument("--expected-identity")
    args = parser.parse_args()
    sys.path.insert(0, str(Path(args.backend_root).resolve()))
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        from native.backend.reconstruction_review import review_dataset
        result = review_dataset(args.dataset, args.group, args.camera_dataset, args.expected_identity)
    except Exception as exc:
        result = dict(schema="gsw.reconstruction_review/1", version=1, ready=False, diagnostic=str(exc))
    print(json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":")))
    return 0 if result["ready"] else 1


if __name__ == "__main__":
    sys.exit(main())
