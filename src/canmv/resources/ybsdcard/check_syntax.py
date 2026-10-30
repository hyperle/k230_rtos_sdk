import ast, sys

files_to_check = [
    'apps/code_det/base_demo_page.py',
    'apps/ai_objects/self_learning/self_learning.py',
    'apps/ai_face/base_demo_page.py',
    'apps/ai_face/face_det_core/face_det.py',
    'apps/ai_body/base_demo_page.py',
    'apps/graphics_det/base_demo_page.py',
]

for f in files_to_check:
    try:
        with open(f, 'r', encoding='utf-8') as fh:
            src = fh.read()
        compile(src, f, 'exec')
        print(f'OK: {f}')
    except SyntaxError as e:
        print(f'SYNTAX ERROR: {f}: line {e.lineno}: {e.msg}')
