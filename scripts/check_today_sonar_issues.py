#!/usr/bin/env python3
"""Check for SonarQube issues created today and their relation to clang-tidy warnings."""

import json
import sys
from datetime import datetime
from pathlib import Path

def main():
    json_path = Path(__file__).parent.parent / "sonar-results" / "sonarqube_issues.json"
    
    if not json_path.exists():
        print(f"Error: {json_path} not found")
        return 1
    
    with open(json_path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    # Handle both list and dict formats
    if isinstance(data, list):
        issues = data
    elif isinstance(data, dict) and 'issues' in data:
        issues = data['issues']
    else:
        print(f"Error: Unexpected JSON structure")
        return 1
    
    today = datetime.now().strftime('%Y-%m-%d')
    today_issues = [i for i in issues if isinstance(i, dict) and i.get('creationDate', '').startswith(today)]
    
    print(f"SonarQube Issues Created Today ({today}): {len(today_issues)}\n")
    
    if not today_issues:
        print("No issues created today.")
        return 0
    
    # Group by rule
    rules = {}
    for issue in today_issues:
        rule = issue.get('rule', 'unknown')
        rules[rule] = rules.get(rule, 0) + 1
    
    print("Rules with new issues today:")
    for rule, count in sorted(rules.items(), key=lambda x: -x[1]):
        print(f"  {rule}: {count}")
    
    # Group by file
    files = {}
    for issue in today_issues:
        component = issue.get('component', '')
        if ':' in component:
            file_path = component.split(':')[-1]
        else:
            file_path = component
        files[file_path] = files.get(file_path, 0) + 1
    
    print(f"\nFiles with new issues today (top 15):")
    for file_path, count in sorted(files.items(), key=lambda x: -x[1])[:15]:
        print(f"  {file_path}: {count}")
    
    # Check files we fixed today
    fixed_files = ['PathStorage.cpp', 'PathStorage.h', 'PathOperations.cpp', 'PathOperations.h', 'FileIndex.cpp']
    fixed_issues = [
        i for i in today_issues
        if any(fixed_file in i.get('component', '') for fixed_file in fixed_files)
    ]
    
    print(f"\nIssues in files we fixed today: {len(fixed_issues)}")
    if fixed_issues:
        print("\nDetails:")
        for issue in fixed_issues[:20]:
            component = issue.get('component', '')
            file_path = component.split(':')[-1] if ':' in component else component
            rule = issue.get('rule', 'unknown')
            message = issue.get('message', '')[:70]
            line = issue.get('line', '?')
            print(f"  {rule}: {message}... ({file_path}:{line})")
    
    # Map SonarQube rules to clang-tidy checks we fixed
    rule_mapping = {
        'cpp:S107': 'Function parameters (we fixed parameter naming)',
        'cpp:S6004': 'Init-statements (we fixed member initialization)',
        'cpp:S1172': 'Unused parameters (we fixed const correctness)',
        'cpp:S995': 'Const references (we fixed const correctness)',
        'cpp:S5350': 'Const references (we fixed const correctness)',
        'cpp:S5008': 'void* pointers (not directly related)',
        'cpp:S134': 'Nesting depth (we fixed else-after-return)',
        'cpp:S3776': 'Cognitive complexity (we added NOLINT)',
        'cpp:S1066': 'Nested ifs (we fixed branch-clone)',
        'cpp:S1135': 'TODO comments (not directly related)',
        'cpp:S1181': 'Exception handling (not directly related)',
        'cpp:S2486': 'Exception handling (not directly related)',
        'cpp:S3630': 'reinterpret_cast (not directly related)',
        'cpp:S924': 'Nested breaks (not directly related)',
        'cpp:S5945': 'C-style arrays (not directly related)',
        'cpp:S3230': 'In-class initializers (we fixed member initialization)',
        'cpp:S5421': 'Global const (not directly related)',
    }
    
    print(f"\nRule mapping to clang-tidy fixes:")
    for rule in sorted(rules.keys()):
        mapping = rule_mapping.get(rule, 'Not directly mapped')
        count = rules[rule]
        print(f"  {rule} ({count} issues): {mapping}")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
