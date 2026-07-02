import json

base_content = ""
patches = []

with open('/home/ubuntu/.gemini/antigravity-ide/brain/10422216-5379-4a3e-a9e1-7034df8a4b9e/.system_generated/logs/transcript_full.jsonl', 'r') as f:
    for line in f:
        try:
            data = json.loads(line)
            if 'tool_calls' in data:
                for call in data['tool_calls']:
                    if call['name'] == 'write_to_file':
                        if 'installer.c' in call['args'].get('TargetFile', ''):
                            base_content = call['args'].get('CodeContent', '')
                            patches = []
                    elif call['name'] == 'replace_file_content':
                        if 'installer.c' in call['args'].get('TargetFile', ''):
                            patches.append([call['args']])
                    elif call['name'] == 'multi_replace_file_content':
                        if 'installer.c' in call['args'].get('TargetFile', ''):
                            patches.append(call['args'].get('ReplacementChunks', []))
        except:
            pass

content = base_content

for patch_group in patches:
    for patch in patch_group:
        target = patch.get('TargetContent', '')
        replacement = patch.get('ReplacementContent', '')
        # Only replace the FIRST occurrence unless AllowMultiple is true
        if target in content:
            allow_multiple = patch.get('AllowMultiple', False)
            content = content.replace(target, replacement, -1 if allow_multiple else 1)
        else:
            print(f"FAILED TO APPLY PATCH! Target not found.")

with open('src/installer.c', 'w') as f:
    f.write(content)
print(f"Recovered {len(content)} bytes to src/installer.c")
