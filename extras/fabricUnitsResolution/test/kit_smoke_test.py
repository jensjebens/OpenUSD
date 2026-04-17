#!/usr/bin/env python3
"""Quick smoke test — can Kit start with omni.usd and quit?"""
import omni.kit.app
print("KIT STARTED SUCCESSFULLY")
omni.kit.app.get_app().post_quit()
