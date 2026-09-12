from pathlib import Path
import re


root = Path(__file__).resolve().parents[1]
legacy = r"selectedCommitHash|selectedFilePath|selectedFileStaged|fullFilePath|fullFileRevision|diffTargetFile|fullFileTargetLine|comparisonScope|comparisonOpen"
mutations = re.compile(r"(?:\.|->)(?:" + legacy + r")\(\)\s*(?:=(?!=)|\.clear\(|\.assign\(|\.append\()")
workspace = re.compile(r"\.workspace(?:\(\)|_)\.(?:open|activate|step|close_source|reset|return_to_review|resolve_source|resolve_review|resolve_comparison|clear_source_reveal)\(")
failures = []
for path in (root / "src").rglob("*"):
    if path.suffix not in {".h", ".cpp", ".mm"}:
        continue
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        if mutations.search(line) or re.search(r"\b(?:activeContent|NavigationHistory|NavigationLocation|commitParents|select_review_target|restore_draft_selection)\b", line):
            failures.append(f"{path.relative_to(root)}:{line_number}: legacy navigation authority")
        if path != root / "src/util/navigation.h" and workspace.search(line):
            failures.append(f"{path.relative_to(root)}:{line_number}: workspace mutation outside navigation boundary")
assert not failures, "\n".join(failures)
print("Navigation boundary ownership checks passed")
