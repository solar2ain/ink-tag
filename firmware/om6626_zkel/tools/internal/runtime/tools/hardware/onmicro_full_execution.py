"""Pure readback prediction of the maintained three-stage loader. No device IO."""
from onmicro_full_writer import validate,_pages,CODE_ERASE_END
from onmicro_rom_loader_model import predict_rom_metadata

def expected_stages(plan):
    validate(plan)
    if any(s.address+len(s.data)>0x400000+CODE_ERASE_END for s in plan.stages[1].segments):
        raise ValueError('code exceeds current erase coverage 0x00477FFF')
    code={s.address+i:b for s in plan.stages[1].segments for i,b in enumerate(s.data)}
    expected=plan.source;result={}
    for stage in plan.stages:
        image=bytearray(expected)
        if stage.name=='factory-code':image[0x4000:CODE_ERASE_END]=b'\xff'*(CODE_ERASE_END-0x4000)
        for at,data in _pages(stage).items():image[at:at+4096]=data
        expected=bytes(image)
        if stage.name=='factory-code':expected,_=predict_rom_metadata(plan.source,expected,code)
        result[stage.name]=expected
    return result
