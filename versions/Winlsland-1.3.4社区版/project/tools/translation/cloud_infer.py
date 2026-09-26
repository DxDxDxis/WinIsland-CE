"""One-request worker. Process exit releases decoder memory on a small server."""
import gc
import json
import os
from pathlib import Path
import re
import sys

os.environ['OMP_NUM_THREADS'] = '2'
sys.path.insert(0, '/extra')
ROOT = Path('/models')
CODES = ['zh-Hans', 'zh-Hant', 'en', 'en-US', 'ja', 'ko', 'ru', 'hi', 'de']

def translate(q):
    import ctranslate2
    import sentencepiece
    from opencc import OpenCC
    from langdetect import detect, DetectorFactory
    DetectorFactory.seed = 0
    text = q['q']
    source, target = q.get('source', 'auto'), q['target']
    aliases = {'zh': 'zh-Hans', 'zh-CN': 'zh-Hans', 'zh-TW': 'zh-Hant'}
    source, target = aliases.get(source, source), aliases.get(target, target)
    if target not in CODES or (source != 'auto' and source not in CODES):
        raise ValueError('Unsupported language code')
    t2s, s2t = OpenCC('t2s'), OpenCC('s2t')
    if source == 'auto':
        if re.search(r'[\u3040-\u30ff]', text): source = 'ja'
        elif re.search(r'[\uac00-\ud7af]', text): source = 'ko'
        elif re.search(r'[\u0900-\u097f]', text): source = 'hi'
        elif re.search(r'[\u0400-\u04ff]', text): source = 'ru'
        elif re.search(r'[\u3400-\u9fff]', text): source = 'zh-Hant' if t2s.convert(text) != text else 'zh-Hans'
        else:
            try: source = detect(text)
            except Exception: source = 'en'
            if source not in ('de','en'): source = 'en'
    detected = source
    norm = lambda s: 'zh' if s.startswith('zh') else 'en' if s == 'en-US' else s
    src, dst = norm(source), norm(target)
    if src == 'zh': text = t2s.convert(text)
    def decode(text, a, b):
        model = ROOT / (a + '-' + b)
        tokenizer = sentencepiece.SentencePieceProcessor(model_file=str(model / 'sentencepiece.model'))
        engine = ctranslate2.Translator(str(model / 'model'), device='cpu', compute_type='int8', inter_threads=1, intra_threads=2)
        output=[]
        for line in text.splitlines(keepends=True):
            tail='\n' if line.endswith('\n') else ''
            body=line.rstrip('\r\n')
            if not body.strip(): output.append(body+tail); continue
            parts=[]
            for sentence in filter(None,re.split(r'(?<=[。！？.!?])\s*',body)):
                tokens=tokenizer.encode(sentence,out_type=str)
                for start in range(0,len(tokens),180):
                    result=engine.translate_batch([tokens[start:start+180]],beam_size=2,max_input_length=0,max_decoding_length=512)[0]
                    parts.append(tokenizer.decode(result.hypotheses[0]).replace('\u2581',' ').strip())
            output.append(('' if b in ('zh','ja') else ' ').join(parts)+tail)
        engine.unload_model();del engine;gc.collect()
        return ''.join(output)
    route=[]
    if src != dst:
        if src != 'en' and dst != 'en':
            text=decode(text,src,'en');route.append(src+'-en');src='en'
        text=decode(text,src,dst);route.append(src+'-'+dst)
    if target=='zh-Hant': text=s2t.convert(text)
    return {'translatedText':text,'detectedLanguage':{'language':detected},'route':route,'scriptConversion':target=='zh-Hant' or detected=='zh-Hant'}

if __name__=='__main__':
    try:
        q=json.loads(sys.stdin.buffer.read(32769));result=translate(q)
    except Exception as exc:
        result={'error':str(exc)}
    sys.stdout.write(json.dumps(result,ensure_ascii=False))
