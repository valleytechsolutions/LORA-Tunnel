"""Address prefixes the CardputerZero adds to the firmware's vendor table: (prefix, family, confidence, evidence).

Each prefix is an IEEE MA-L assignment to the named organization (IEEE public registry). A registered
organization does not establish the product, its owner or what it is doing.
"""

EXTRA_PREFIXES = tuple(sorted((
    (0x000e58, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x347e5c, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x38420b, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x48a6b8, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x542a1b, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x5caafd, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x74ca60, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x7828ca, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x804af2, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x949f3e, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0xb8e937, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0xc43875, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0xf0f6c1, "Listener", "Medium", "Sonos OUI; speaker with microphones, activity unverified"),
    (0x003044, "Fleet", "Low", "CradlePoint OUI; vehicle or fleet router, not proof of police"),
    (0x00e01c, "Fleet", "Low", "CradlePoint OUI; vehicle or fleet router, not proof of police"),
    (0x00a0d5, "Fleet", "Low", "Sierra Wireless OUI; vehicle or fleet router, not proof of police"),
    (0x28a331, "Fleet", "Low", "Sierra Wireless OUI; vehicle or fleet router, not proof of police"),
    (0x64ce6e, "Fleet", "Low", "Sierra Wireless OUI; vehicle or fleet router, not proof of police"),
    (0x84db2f, "Fleet", "Low", "Sierra Wireless OUI; vehicle or fleet router, not proof of police"),
    (0xcc934a, "Fleet", "Low", "Sierra Wireless OUI; vehicle or fleet router, not proof of police"),
    (0x0016b2, "Camera", "Medium", "DriveCam (Lytx) fleet camera OUI; product/activity unverified"),
    (0x2c4205, "Camera", "Medium", "Lytx fleet camera OUI; product/activity unverified"),
    (0x50df95, "Camera", "Medium", "Lytx fleet camera OUI; product/activity unverified"),
    (0x70e46e, "Camera", "Medium", "Lytx fleet camera OUI; product/activity unverified"),
    (0x002542, "Camera", "Medium", "Pittasoft (BlackVue) dashcam OUI; product/activity unverified"),
    (0x001f4f, "Camera", "Medium", "Thinkware dashcam OUI; product/activity unverified"),
    (0x4427f3, "Camera", "Medium", "70mai dashcam OUI; product/activity unverified"),
    (0xb859c8, "Camera", "Medium", "70mai dashcam OUI; product/activity unverified"),
    (0xfcdb21, "Camera", "Low", "Samsara fleet OUI; gateway or camera, product unverified"),
    (0xf85128, "Camera", "Medium", "SimpliSafe OUI; product/activity unverified"),
    (0x98da92, "Meta", "Low", "Vuzix OUI; model/recording unverified"),
)))
