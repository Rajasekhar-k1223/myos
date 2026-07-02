import json

with open('/home/ubuntu/.gemini/antigravity-ide/brain/10422216-5379-4a3e-a9e1-7034df8a4b9e/.system_generated/logs/transcript_full.jsonl', 'r') as f:
    for line in f:
        if 'installer.c' in line:
            try:
                data = json.loads(line)
                if 'tool_calls' in data:
                    for call in data['tool_calls']:
                        if 'installer.c' in str(call):
                            print(f"Tool: {call['name']}")
                            print(f"Args keys: {list(call['args'].keys())}")
            except:
                pass
