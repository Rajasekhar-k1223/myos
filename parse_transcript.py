import json

found = []
with open('/home/ubuntu/.gemini/antigravity-ide/brain/10422216-5379-4a3e-a9e1-7034df8a4b9e/.system_generated/logs/transcript_full.jsonl', 'r') as f:
    for line in f:
        try:
            data = json.loads(line)
            if 'tool_calls' in data:
                for call in data['tool_calls']:
                    if call['name'] == 'write_to_file':
                        if 'installer.c' in call['args'].get('TargetFile', ''):
                            found.append(call['args'].get('CodeContent', ''))
        except:
            pass

if found:
    with open('/tmp/recovered_installer.c', 'w') as f:
        f.write(found[-1])
    print(f"Recovered {len(found[-1])} bytes to /tmp/recovered_installer.c")
else:
    print("Not found")
