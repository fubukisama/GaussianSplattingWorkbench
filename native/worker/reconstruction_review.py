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
    parser.add_argument("--model-directory")
    parser.add_argument("--task-originals", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(Path(args.backend_root).resolve()))
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        from native.backend.reconstruction_review import review_dataset, review_model, review_training_originals
        if args.task_originals:
            if args.model_directory or args.camera_dataset or args.group:
                raise ValueError("task_originals_require_exact_dataset")
            result = review_training_originals(args.dataset, args.expected_identity)
        else:
            result = (review_model(args.model_directory, args.group, args.camera_dataset)
                      if args.model_directory else
                      review_dataset(args.dataset, args.group, args.camera_dataset, args.expected_identity))
    except Exception as exc:
        result = dict(schema="gsw.reconstruction_review/1", version=1, ready=False, diagnostic=str(exc))
    print(json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":")))
    return 0 if result["ready"] else 1


if __name__ == "__main__":
    sys.exit(main())
