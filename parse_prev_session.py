import json

base_content = ""

# First, extract the final state from the previous session
with open('/home/ubuntu/.gemini/antigravity-ide/brain/6bcf6ece-a59f-4ad5-a1fb-e31ef3f56e84/.system_generated/logs/transcript_full.jsonl', 'r') as f:
    for line in f:
        try:
            data = json.loads(line)
            if 'tool_calls' in data:
                for call in data['tool_calls']:
                    if call['name'] == 'write_to_file':
                        if 'installer.c' in call['args'].get('TargetFile', ''):
                            base_content = call['args'].get('CodeContent', '')
                    elif call['name'] == 'replace_file_content':
                        if 'installer.c' in call['args'].get('TargetFile', ''):
                            target = call['args'].get('TargetContent', '')
                            rep = call['args'].get('ReplacementContent', '')
                            base_content = base_content.replace(target, rep, 1)
                    elif call['name'] == 'multi_replace_file_content':
                        if 'installer.c' in call['args'].get('TargetFile', ''):
                            for chunk in call['args'].get('ReplacementChunks', []):
                                target = chunk.get('TargetContent', '')
                                rep = chunk.get('ReplacementContent', '')
                                base_content = base_content.replace(target, rep, 1)
        except:
            pass

print(f"Extracted {len(base_content)} bytes from previous session.")
with open('/tmp/prev_installer.c', 'w') as f:
    f.write(base_content)

