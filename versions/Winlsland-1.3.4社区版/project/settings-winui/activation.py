"""Mirror GenerateAppManifestFromAppx from the official SelfContained targets."""
from pathlib import Path
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
ns={'m':'http://schemas.microsoft.com/appx/manifest/foundation/windows10'}
source=ET.parse(root/'dependency-cache/winui/framework/AppxManifest.xml')
ET.register_namespace('','urn:schemas-microsoft-com:asm.v1')
ET.register_namespace('asmv3','urn:schemas-microsoft-com:asm.v3')
ET.register_namespace('winrtv1','urn:schemas-microsoft-com:winrt.v1')
manifest=ET.Element('{urn:schemas-microsoft-com:asm.v1}assembly',manifestVersion='1.0')
for server in source.findall('./m:Extensions/m:Extension/m:InProcessServer',ns):
 file=ET.SubElement(manifest,'{urn:schemas-microsoft-com:asm.v3}file',name=server.find('m:Path',ns).text)
 for cls in server.findall('m:ActivatableClass',ns):
  ET.SubElement(file,'{urn:schemas-microsoft-com:winrt.v1}activatableClass',name=cls.attrib['ActivatableClassId'],threadingModel='both')
ET.ElementTree(manifest).write(root/'settings-winui/generated/runtime.manifest',encoding='utf-8',xml_declaration=True)
