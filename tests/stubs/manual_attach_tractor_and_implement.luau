    _G.tractor = StubManualAttach.tractor({})
    _G.plow = StubManualAttach.implement('plow')
    StubManualAttach.lineUp(plow, tractor, 1)
    StubManualAttach.detectAll()
