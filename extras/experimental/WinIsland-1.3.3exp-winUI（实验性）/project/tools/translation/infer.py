"""Isolated, stdin/stdout-only CPU inference. No network, telemetry, or text files."""
import json
import os
from pathlib import Path
import re
import sys

os.environ['OMP_NUM_THREADS'] = '2'
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent

def main():
    import ctranslate2
    import sentencepiece
    q = json.loads(sys.stdin.buffer.read(32769))
    text = q['q']
    if not isinstance(text, str) or not text.strip() or len(text) > 4000:
        raise ValueError('请提供 1–4000 字符的文本；长文请分段翻译')
    source, target = q.get('source', 'auto'), q['target']
    normalize = lambda s: {'zh': 'zh', 'zh-Hans': 'zh', 'en-US': 'en'}.get(s, s)
    source, target = normalize(source), normalize(target)
    if source == 'auto':
        source = 'zh' if re.search(r'[\u3400-\u9fff]', text) else 'en'
    if source not in ('en', 'zh') or target not in ('en', 'zh'):
        raise ValueError('此离线包仅支持简体中文与英文互译')
    if source == target:
        return {'translatedText': text, 'detectedLanguage': {'language': source}}
    model = ROOT / 'models' / (source + '-' + target)
    # Native dependencies do not consistently accept non-ASCII Windows paths.
    # Python opens the tokenizer; the decoder receives an ASCII relative path.
    sp = sentencepiece.SentencePieceProcessor(model_proto=(model / 'sentencepiece.model').read_bytes())
    os.chdir(model)
    translator = ctranslate2.Translator('model', device='cpu', compute_type='int8', inter_threads=1, intra_threads=2)
    output = []
    # Preserve paragraph separators. Bound token batches rather than silently truncate.
    for paragraph in text.splitlines(keepends=True):
        newline = '\n' if paragraph.endswith('\n') else ''
        body = paragraph.rstrip('\r\n')
        if not body.strip():
            output.append(body + newline)
            continue
        sentences = re.split(r'(?<=[。！？.!?])\s*', body)
        parts = []
        for sentence in filter(None, sentences):
            tokens = sp.encode(sentence, out_type=str)
            for start in range(0, len(tokens), 180):
                result = translator.translate_batch([tokens[start:start+180]], beam_size=2, max_input_length=0, max_decoding_length=512)[0]
                parts.append(sp.decode(result.hypotheses[0]).replace('\u2581', ' ').strip())
        output.append((' ' if target == 'en' else '').join(parts) + newline)
    return {'translatedText': ''.join(output), 'detectedLanguage': {'language': source}}

if __name__ == '__main__':
    try:
        result = main()
    except Exception as exc:
        result = {'error': str(exc)}
    sys.stdout.buffer.write(json.dumps(result, ensure_ascii=False).encode('utf-8'))
