import json

found = []
with open('/home/ubuntu/.gemini/antigravity-ide/brain/10422216-5379-4a3e-a9e1-7034df8a4b9e/.system_generated/logs/transcript_full.jsonl', 'r') as f:
    for line in f:
        try:
            data = json.loads(line)
            if 'tool_calls' in data:
                for call in data['tool_calls']:
                    if call['name'] == 'run_command':
                        cmd = call['args'].get('CommandLine', '')
                        if 'cat << \'EOF\' > src/installer.c' in cmd or 'cat << "EOF" > src/installer.c' in cmd:
                            found.append(cmd)
        except:
            pass

if found:
    print(f"Found {len(found)} script creations.")
    with open('/tmp/recovered.sh', 'w') as f:
        f.write(found[0])
else:
    print("Not found")
