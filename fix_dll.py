import urllib.request
import zipfile
import os
import shutil
import tempfile

# 1. 깃허브 공식 백업망(Releases)에서 nupkg(압축파일) 직접 다운로드 경로
url = "https://github.com/ros2/choco-packages/releases/download/2022-03-15/tinyxml2.6.0.0.nupkg"
tmp_zip = os.path.join(tempfile.gettempdir(), "tiny_nupkg.zip")
tmp_dir = os.path.join(tempfile.gettempdir(), "tiny_ext")

print("1. 공식 GitHub에서 tinyxml2 패키지 다운로드 중...")
urllib.request.urlretrieve(url, tmp_zip)

print("2. 압축 해제 및 파일 찾는 중...")
with zipfile.ZipFile(tmp_zip, 'r') as z:
    z.extractall(tmp_dir)

# 내부에 또 zip 파일이 있으면 압축 해제 (초코레이티 패키지 특성)
for root, dirs, files in os.walk(tmp_dir):
    for f in files:
        if f.lower().endswith(".zip"):
            with zipfile.ZipFile(os.path.join(root, f), 'r') as z:
                z.extractall(root)

print("3. 오리지널 tinyxml2.dll 추출 및 복사 중...")
dll_path = None
for root, dirs, files in os.walk(tmp_dir):
    for f in files:
        if f.lower() == "tinyxml2.dll":
            dll_path = os.path.join(root, f)
            break
    if dll_path: break

if dll_path:
    # 사용자님의 실행 폴더로 정품 DLL 강제 이식!
    target = r"C:\Users\ckd30\Projects\Maya_Ros_Sim\install\control_bridge\lib\control_bridge\tinyxml2.dll"
    shutil.copy(dll_path, target)
    print("\n🎉 대성공! 오리지널 DLL이 완벽하게 복사되었습니다!")
else:
    print("\n❌ 실패: 파일을 찾지 못했습니다.")