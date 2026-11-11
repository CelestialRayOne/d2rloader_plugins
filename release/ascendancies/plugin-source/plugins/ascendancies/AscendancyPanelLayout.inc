// The ascendancy window layout (data/global/ui/layouts/celestialrayone.ascendancies/
// AscendancyPanelhd.json), compiled into the plugin. Generated; split into pieces below
// MSVC's string literal limit. The plugin joins them and registers the result.
constexpr const char* const PanelLayoutChunks[] = {
R"ASCJ({
 "type": "Panel",
 "name": "celestialrayone.ascendancies/AscendancyPanel",
 "fields": {
  "anchor": {
   "x": 0.5,
   "y": 0.0
  },
  "priority": 8500
 },
 "children": [
  {
   "type": "ClickCatcherWidget",
   "name": "ClickCatcher",
   "fields": {
    "rect": {
     "x": -815,
     "y": 13,
     "width": 1630,
     "height": 1724
    }
   }
  },
  {
   "type": "ImageWidget",
   "name": "Background",
   "fields": {
    "rect": {
     "x": -815,
     "y": 13
    },
    "filename": "PANEL\\Ascendancies\\Background"
   }
  },
  {
   "type": "ButtonWidget",
   "name": "Close",
   "fields": {
    "rect": {
     "x": 733,
     "y": 14
    },
    "filename": "PANEL\\closebtn_4x",
    "hoveredFrame": 3,
    "tooltipString": "@d2r:strClose",
    "onClickMessage": "PanelManager:OpenPanel:AscendanciesClose",
    "sound": "cursor_close_window_hd"
   }
  },
  {
   "type": "ButtonWidget",
   "name": "Previous",
   "fields": {
    "rect": {
     "x": -743,
     "y": 85
    },
    "filename": "PANEL\\Ascendancies\\Button",
    "hoveredFrame": 1,
    "pressedFrame": 2,
    "onClickMessage": "PanelManager:OpenPanel:AscendanciesPrevious"
   }
  },
  {
   "type": "TextBoxWidget",
   "name": "PreviousLabel",
   "fields": {
    "rect": {
     "x": -743,
     "y": 85,
     "width": 280,
     "height": 84
    },
    "text": "@d2r:AscendancyPrevious",
    "style": {
     "fontColor": {
      "r": 214,
      "g": 180,
      "b": 128,
      "a": 255
     },
     "pointSize": "$SmallMediumFontSize",
     "alignment": {
      "h": "center",
      "v": "center"
     },
     "dropShadow": "$DefaultDropShadow"
    }
   }
  },
  {
   "type": "ButtonWidget",
   "name": "Next",
   "fields": {
    "rect": {
     "x": 463,
     "y": 85
    },
    "filename": "PANEL\\Ascendancies\\Button",
    "hoveredFrame": 1,
    "pressedFrame": 2,
    "onClickMessage": "PanelManager:OpenPanel:AscendanciesNext"
   }
  },
  {
   "type": "TextBoxWidget",
   "name": "NextLabel",
   "fields": {
    "rect": {
     "x": 463,
     "y": 85,
     "width": 280,
     "height": 84
    },
    "text": "@d2r:AscendancyNext",
    "style": {
     "fontColor": {
      "r": 214,
      "g": 180,
      "b": 128,
      "a": 255
     },
     "pointSize": "$SmallMediumFontSize",
     "alignment": {
      "h": "center",
      "v": "center"
     },
     "dropShadow": "$DefaultDropShadow"
    }
   }
  },
  {
   "type": "TextBoxWidget",
   "name": "Title",
   "fields": {
    "rect": {
     "x": -400,
     "y": 95,
     "width": 800,
     "height": 64
    },
    "text": "@d2r:AscendanciesTitle",
    "style": "$StyleTitleBlock"
   }
  },
  {
   "type": "TextBoxWidget",
   "name": "Name",
   "fields": {
    "rect": {
     "x": -600,
     "y": 222,
     "width": 1200,
     "height": 52
    },
    "text": "",
    "style": {
     "fontColor": {
      "r": 255,
      "g": 165,
      "b": 0,
      "a": 255
     },
     "pointSize": "$XMediumLargeFontSize",
     "alignment": {
      "h": "center",
      "v": "center"
     },
     "dropShadow": "$DefaultDropShadow"
    }
   }
  },
  {
   "type": "Widget",
   "name": "Portraits",
   "fields": {
    "rect": {
     "x": -815,
     "y": 13,
     "width": 1630,
     "height": 1724
    }
   },
   "children": [
    {
     "type": "ImageWidget",
     "name": "Portrait1",
     "fields": {
      "rect": {
       "x": 938,
       "y": 510
      },
      "filename": "Ascendancies\\battlemage"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait2",
     "fields": {
      "rect": {
       "x": 171,
       "y": 510
      },
      "filename": "Ascendancies\\blademaster"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait3",
     "fields": {
      "rect": {
       "x": 938,
       "y": 510
      },
      "filename": "Ascendancies\\arcanist"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait4",
     "fields": {
      "rect": {
       "x": 171,
       "y": 510
      },
      "filename": "Ascendancies\\bloodmage"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait5",
     "fields": {
      "rect": {
       "x": 938,
       "y": 510
      },
      "filename": "Ascendancies\\awakened"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait6",
     "fields": {
      "rect": {
       "x": 171,
       "y": 510
      },
      "filename": "Ascendancies\\ironbolt"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait7",
     "fields": {
      "rect": {
       "x": 938,
       "y": 510
      },
      "filename": "Ascendancies\\starborn"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait8",
     "fields": {
      "rect": {
       "x": 171,
       "y": 510
      },
      "filename": "Ascendancies\\lifebreaker"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait9",
     "fields": {
      "rect": {
       "x": 938,
       "y": 510
      },
      "filename": "Ascendancies\\worldshaper"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait10",
     "fields": {
      "rect": {
       "x": 171,
       "y": 510
      },
      "filename": "Ascendancies\\soulwarden"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait11",
     "fields": {
      "rect": {
       "x": 938,
       "y": 510
      },
      "filename": "Ascendancies\\stancedancer"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait12",
     "fields": {
      "rect": {
       "x": 171,
       "y": 510
      },
      "filename": "Ascendancies\\nomad"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait13",
     "fields": {
      "rect": {
       "x": 938,
       "y": 510
      },
      "filename": "Ascendancies\\sharpshooter"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait14",
     "fields": {
      "rect": {
       "x": 171,
       "y": 510
      },
      "filename": "Ascendancies\\manawarden"
     }
    },
    {
     "type": "ImageWidget",
     "name": "Portrait15",
     "fields": {
      "rect": {
       "x": 938,
       "y": 510
      },
      "filename": "Ascendancies\\berserker"
     }
    }
   ]
  },
  {
   "type": "Widget",
   "name": "L",
   "fields": {
    "rect": {
     "x": -704,
     "y": 373,
     "width": 804,
     "height": 1080
    }
   },
   "children": [
    {
     "type": "Widget",
     "name": "L1",
     "fields": {
      "rect": {
       "x": 0,
       "y": 0,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L2",
     "fields": {
      "rect": {
       "x": 0,
       "y": 27,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L3",
     "fields": {
      "rect": {
       "x": 0,
       "y": 54,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L4",
     "fields": {
      "rect": {
       "x": 0,
       "y": 81,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
)ASCJ",
R"ASCJ(        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L5",
     "fields": {
      "rect": {
       "x": 0,
       "y": 108,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L6",
     "fields": {
      "rect": {
       "x": 0,
       "y": 135,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L7",
     "fields": {
      "rect": {
       "x": 0,
       "y": 162,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L8",
     "fields": {
      "rect": {
       "x": 0,
       "y": 189,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L9",
     "fields": {
      "rect": {
       "x": 0,
       "y": 216,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L10",
     "fields": {
      "rect": {
       "x": 0,
       "y": 243,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
)ASCJ",
R"ASCJ(          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L11",
     "fields": {
      "rect": {
       "x": 0,
       "y": 270,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L12",
     "fields": {
      "rect": {
       "x": 0,
       "y": 297,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L13",
     "fields": {
      "rect": {
       "x": 0,
       "y": 324,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L14",
     "fields": {
      "rect": {
       "x": 0,
       "y": 351,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L15",
     "fields": {
      "rect": {
       "x": 0,
       "y": 378,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L16",
     "fields": {
      "rect": {
       "x": 0,
       "y": 405,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L17",
     "fields": {
      "rect": {
       "x": 0,
       "y": 432,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
)ASCJ",
R"ASCJ(         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L18",
     "fields": {
      "rect": {
       "x": 0,
       "y": 459,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L19",
     "fields": {
      "rect": {
       "x": 0,
       "y": 486,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L20",
     "fields": {
      "rect": {
       "x": 0,
       "y": 513,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L21",
     "fields": {
      "rect": {
       "x": 0,
       "y": 540,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L22",
     "fields": {
      "rect": {
       "x": 0,
       "y": 567,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L23",
     "fields": {
      "rect": {
       "x": 0,
       "y": 594,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
)ASCJ",
R"ASCJ(        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L24",
     "fields": {
      "rect": {
       "x": 0,
       "y": 621,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L25",
     "fields": {
      "rect": {
       "x": 0,
       "y": 648,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L26",
     "fields": {
      "rect": {
       "x": 0,
       "y": 675,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L27",
     "fields": {
      "rect": {
       "x": 0,
       "y": 702,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L28",
     "fields": {
      "rect": {
       "x": 0,
       "y": 729,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L29",
     "fields": {
      "rect": {
       "x": 0,
       "y": 756,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
)ASCJ",
R"ASCJ(          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L30",
     "fields": {
      "rect": {
       "x": 0,
       "y": 783,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L31",
     "fields": {
      "rect": {
       "x": 0,
       "y": 810,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L32",
     "fields": {
      "rect": {
       "x": 0,
       "y": 837,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L33",
     "fields": {
      "rect": {
       "x": 0,
       "y": 864,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L34",
     "fields": {
      "rect": {
       "x": 0,
       "y": 891,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L35",
     "fields": {
      "rect": {
       "x": 0,
       "y": 918,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
)ASCJ",
R"ASCJ(          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L36",
     "fields": {
      "rect": {
       "x": 0,
       "y": 945,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L37",
     "fields": {
      "rect": {
       "x": 0,
       "y": 972,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L38",
     "fields": {
      "rect": {
       "x": 0,
       "y": 999,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L39",
     "fields": {
      "rect": {
       "x": 0,
       "y": 1026,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "L40",
     "fields": {
      "rect": {
       "x": 0,
       "y": 1053,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    }
   ]
  },
  {
   "type": "Widget",
   "name": "R",
   "fields": {
    "rect": {
     "x": -101,
     "y": 373,
     "width": 804,
     "height": 1080
    }
   },
   "children": [
    {
     "type": "Widget",
     "name": "R1",
     "fields": {
      "rect": {
       "x": 0,
       "y": 0,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R2",
     "fields": {
      "rect": {
       "x": 0,
       "y": 27,
       "width": 804,
       "height": 27
      }
     },
     "children": [
)ASCJ",
R"ASCJ(      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R3",
     "fields": {
      "rect": {
       "x": 0,
       "y": 54,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R4",
     "fields": {
      "rect": {
       "x": 0,
       "y": 81,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R5",
     "fields": {
      "rect": {
       "x": 0,
       "y": 108,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R6",
     "fields": {
      "rect": {
       "x": 0,
       "y": 135,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R7",
     "fields": {
      "rect": {
       "x": 0,
       "y": 162,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R8",
     "fields": {
      "rect": {
       "x": 0,
       "y": 189,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
)ASCJ",
R"ASCJ(       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R9",
     "fields": {
      "rect": {
       "x": 0,
       "y": 216,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R10",
     "fields": {
      "rect": {
       "x": 0,
       "y": 243,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R11",
     "fields": {
      "rect": {
       "x": 0,
       "y": 270,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R12",
     "fields": {
      "rect": {
       "x": 0,
       "y": 297,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R13",
     "fields": {
      "rect": {
       "x": 0,
       "y": 324,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R14",
     "fields": {
      "rect": {
       "x": 0,
       "y": 351,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
)ASCJ",
R"ASCJ(         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R15",
     "fields": {
      "rect": {
       "x": 0,
       "y": 378,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R16",
     "fields": {
      "rect": {
       "x": 0,
       "y": 405,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R17",
     "fields": {
      "rect": {
       "x": 0,
       "y": 432,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R18",
     "fields": {
      "rect": {
       "x": 0,
       "y": 459,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R19",
     "fields": {
      "rect": {
       "x": 0,
       "y": 486,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R20",
     "fields": {
      "rect": {
       "x": 0,
       "y": 513,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
)ASCJ",
R"ASCJ(        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R21",
     "fields": {
      "rect": {
       "x": 0,
       "y": 540,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R22",
     "fields": {
      "rect": {
       "x": 0,
       "y": 567,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R23",
     "fields": {
      "rect": {
       "x": 0,
       "y": 594,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R24",
     "fields": {
      "rect": {
       "x": 0,
       "y": 621,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R25",
     "fields": {
      "rect": {
       "x": 0,
       "y": 648,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R26",
     "fields": {
      "rect": {
       "x": 0,
       "y": 675,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R27",
     "fields": {
      "rect": {
       "x": 0,
       "y": 702,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
)ASCJ",
R"ASCJ(       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R28",
     "fields": {
      "rect": {
       "x": 0,
       "y": 729,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R29",
     "fields": {
      "rect": {
       "x": 0,
       "y": 756,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R30",
     "fields": {
      "rect": {
       "x": 0,
       "y": 783,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R31",
     "fields": {
      "rect": {
       "x": 0,
       "y": 810,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R32",
     "fields": {
      "rect": {
       "x": 0,
       "y": 837,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R33",
     "fields": {
      "rect": {
       "x": 0,
       "y": 864,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
)ASCJ",
R"ASCJ(         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R34",
     "fields": {
      "rect": {
       "x": 0,
       "y": 891,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R35",
     "fields": {
      "rect": {
       "x": 0,
       "y": 918,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R36",
     "fields": {
      "rect": {
       "x": 0,
       "y": 945,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R37",
     "fields": {
      "rect": {
       "x": 0,
       "y": 972,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R38",
     "fields": {
      "rect": {
       "x": 0,
       "y": 999,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R39",
     "fields": {
      "rect": {
       "x": 0,
       "y": 1026,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
)ASCJ",
R"ASCJ(         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    },
    {
     "type": "Widget",
     "name": "R40",
     "fields": {
      "rect": {
       "x": 0,
       "y": 1053,
       "width": 804,
       "height": 27
      }
     },
     "children": [
      {
       "type": "TextBoxWidget",
       "name": "HeadOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 255,
          "g": 165,
          "b": 0,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "HeadOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": -12,
         "width": 784,
         "height": 35
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 95,
          "b": 30,
          "a": 255
         },
         "pointSize": "$XMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOn",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 238,
          "g": 226,
          "b": 196,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      },
      {
       "type": "TextBoxWidget",
       "name": "LineOff",
       "fields": {
        "rect": {
         "x": 10,
         "y": 0,
         "width": 784,
         "height": 27
        },
        "text": "",
        "style": {
         "fontColor": {
          "r": 140,
          "g": 134,
          "b": 120,
          "a": 255
         },
         "pointSize": "$SmallMediumFontSize",
         "alignment": {
          "h": "center",
          "v": "center"
         },
         "dropShadow": "$DefaultDropShadow"
        }
       }
      }
     ]
    }
   ]
  },
  {
   "type": "TextBoxWidget",
   "name": "Footer",
   "fields": {
    "rect": {
     "x": -767,
     "y": 1497,
     "width": 1534,
     "height": 56
    },
    "text": "",
    "style": {
     "fontColor": {
      "r": 214,
      "g": 180,
      "b": 128,
      "a": 255
     },
     "pointSize": "$SmallMediumFontSize",
     "alignment": {
      "h": "center",
      "v": "center"
     },
     "dropShadow": "$DefaultDropShadow",
     "options": {
      "lineWrap": true,
      "newlineHandling": "standard"
     }
    }
   }
  },
  {
   "type": "ButtonWidget",
   "name": "Action",
   "fields": {
    "rect": {
     "x": -250,
     "y": 1581
    },
    "filename": "PANEL\\Ascendancies\\ButtonWide",
    "hoveredFrame": 1,
    "pressedFrame": 2,
    "onClickMessage": "PanelManager:OpenPanel:AscendanciesAction"
   }
  },
  {
   "type": "TextBoxWidget",
   "name": "ActionLabel",
   "fields": {
    "rect": {
     "x": -250,
     "y": 1581,
     "width": 500,
     "height": 84
    },
    "text": "",
    "style": {
     "fontColor": {
      "r": 214,
      "g": 180,
      "b": 128,
      "a": 255
     },
     "pointSize": "$SmallMediumFontSize",
     "alignment": {
      "h": "center",
      "v": "center"
     },
     "dropShadow": "$DefaultDropShadow"
    }
   }
  },
  {
   "type": "Widget",
   "name": "Confirm",
   "fields": {
    "rect": {
     "x": -380,
     "y": 723,
     "width": 760,
     "height": 380
    }
   },
   "children": [
    {
     "type": "ImageWidget",
     "name": "ConfirmBack",
     "fields": {
      "rect": {
       "x": 0,
       "y": 0
      },
      "filename": "PANEL\\Ascendancies\\Dialog"
     }
    },
    {
     "type": "TextBoxWidget",
     "name": "ConfirmLine1",
     "fields": {
      "rect": {
       "x": 40,
       "y": 34,
       "width": 680,
       "height": 40
      },
      "text": "",
      "style": {
       "fontColor": {
        "r": 238,
        "g": 226,
        "b": 196,
        "a": 255
       },
       "pointSize": "$MediumFontSize",
       "alignment": {
        "h": "center",
        "v": "center"
       },
       "dropShadow": "$DefaultDropShadow"
      }
     }
    },
    {
     "type": "TextBoxWidget",
     "name": "ConfirmLine2",
     "fields": {
      "rect": {
       "x": 40,
       "y": 74,
       "width": 680,
       "height": 40
      },
      "text": "",
      "style": {
       "fontColor": {
        "r": 238,
        "g": 226,
        "b": 196,
        "a": 255
       },
       "pointSize": "$MediumFontSize",
       "alignment": {
        "h": "center",
        "v": "center"
       },
       "dropShadow": "$DefaultDropShadow"
      }
     }
    },
    {
     "type": "TextBoxWidget",
     "name": "ConfirmLine3",
     "fields": {
      "rect": {
       "x": 40,
       "y": 114,
       "width": 680,
       "height": 40
      },
      "text": "",
      "style": {
       "fontColor": {
        "r": 238,
        "g": 226,
        "b": 196,
        "a": 255
       },
       "pointSize": "$MediumFontSize",
       "alignment": {
        "h": "center",
        "v": "center"
       },
       "dropShadow": "$DefaultDropShadow"
      }
     }
    },
    {
     "type": "TextBoxWidget",
     "name": "ConfirmLine4",
     "fields": {
      "rect": {
       "x": 40,
       "y": 154,
       "width": 680,
       "height": 40
      },
      "text": "",
      "style": {
       "fontColor": {
        "r": 238,
        "g": 226,
        "b": 196,
        "a": 255
       },
       "pointSize": "$MediumFontSize",
       "alignment": {
        "h": "center",
        "v": "center"
       },
       "dropShadow": "$DefaultDropShadow"
      }
     }
    },
    {
     "type": "TextBoxWidget",
     "name": "ConfirmLine5",
     "fields": {
      "rect": {
       "x": 40,
       "y": 194,
       "width": 680,
       "height": 40
      },
      "text": "",
      "style": {
       "fontColor": {
        "r": 238,
        "g": 226,
        "b": 196,
        "a": 255
       },
       "pointSize": "$MediumFontSize",
       "alignment": {
        "h": "center",
        "v": "center"
       },
       "dropShadow": "$DefaultDropShadow"
      }
     }
    },
    {
     "type": "ButtonWidget",
     "name": "ConfirmYes",
     "fields": {
      "rect": {
       "x": 80,
       "y": 262
      },
      "filename": "PANEL\\Ascendancies\\Button",
      "hoveredFrame": 1,
      "pressedFrame": 2,
      "onClickMessage": "PanelManager:OpenPanel:AscendanciesYes"
     }
    },
    {
     "type": "TextBoxWidget",
     "name": "ConfirmYesLabel",
     "fields": {
      "rect": {
       "x": 80,
       "y": 262,
       "width": 280,
       "height": 84
      },
      "text": "@d2r:AscendancyConfirmYes",
      "style": {
       "fontColor": {
        "r": 214,
        "g": 180,
        "b": 128,
        "a": 255
       },
       "pointSize": "$SmallMediumFontSize",
       "alignment": {
        "h": "center",
        "v": "center"
       },
       "dropShadow": "$DefaultDropShadow"
      }
     }
    },
    {
     "type": "ButtonWidget",
     "name": "ConfirmNo",
     "fields": {
      "rect": {
       "x": 400,
       "y": 262
      },
      "filename": "PANEL\\Ascendancies\\Button",
      "hoveredFrame": 1,
      "pressedFrame": 2,
      "onClickMessage": "PanelManager:OpenPanel:AscendanciesNo"
     }
    },
    {
     "type": "TextBoxWidget",
     "name": "ConfirmNoLabel",
     "fields": {
      "rect": {
       "x": 400,
       "y": 262,
       "width": 280,
       "height": 84
      },
      "text": "@d2r:AscendancyConfirmNo",
      "style": {
       "fontColor": {
        "r": 214,
        "g": 180,
        "b": 128,
        "a": 255
       },
       "pointSize": "$SmallMediumFontSize",
       "alignment": {
        "h": "center",
        "v": "center"
       },
       "dropShadow": "$DefaultDropShadow"
      }
     }
    }
   ]
  }
 ]
}
)ASCJ",
};
