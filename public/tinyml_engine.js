/**
 * @file tinyml_engine.js
 * @brief Embedded TinyML Random Forest (20 Trees) Inference & Verification Engine.
 * 100% bit-for-bit parity with embedded C99 implementation (fall_trees.c & fall_trees.h).
 * Part of: Secure Wearable Health Monitoring System (ASCON-128 + ESP32 + MPU6050)
 */

(function (window) {
  'use strict';

  // Feature names in canonical extraction order
  const FEATURE_METADATA = [
    { key: "acc_mag_mean", label: "acc_mean", name: "Acc Magnitude Mean", unit: "g", group: "acc", min: 0.0, max: 5.0, step: 0.01, precision: 2, defaultVal: 1.01 },
    { key: "acc_mag_std", label: "acc_std", name: "Acc Magnitude Std Dev", unit: "g", group: "acc", min: 0.0, max: 4.0, step: 0.01, precision: 2, defaultVal: 0.08 },
    { key: "acc_mag_min", label: "acc_min", name: "Acc Magnitude Minimum", unit: "g", group: "acc", min: 0.0, max: 3.0, step: 0.01, precision: 2, defaultVal: 0.88 },
    { key: "acc_mag_max", label: "acc_max", name: "Acc Magnitude Peak Shock", unit: "g", group: "acc", min: 0.0, max: 12.0, step: 0.05, precision: 2, defaultVal: 1.15 },
    { key: "acc_mag_range", label: "acc_range", name: "Acc Magnitude Dynamic Range", unit: "g", group: "acc", min: 0.0, max: 12.0, step: 0.05, precision: 2, defaultVal: 0.27 },
    { key: "acc_mag_rms", label: "acc_rms", name: "Acc Magnitude Root-Mean-Square", unit: "g", group: "acc", min: 0.0, max: 6.0, step: 0.01, precision: 2, defaultVal: 1.01 },

    { key: "gyro_mag_mean", label: "gyro_mean", name: "Gyro Magnitude Mean", unit: "°/s", group: "gyro", min: 0.0, max: 500.0, step: 1.0, precision: 1, defaultVal: 22.4 },
    { key: "gyro_mag_std", label: "gyro_std", name: "Gyro Magnitude Std Dev", unit: "°/s", group: "gyro", min: 0.0, max: 400.0, step: 1.0, precision: 1, defaultVal: 14.2 },
    { key: "gyro_mag_min", label: "gyro_min", name: "Gyro Magnitude Minimum", unit: "°/s", group: "gyro", min: 0.0, max: 100.0, step: 0.5, precision: 1, defaultVal: 4.5 },
    { key: "gyro_mag_max", label: "gyro_max", name: "Gyro Magnitude Peak Rate", unit: "°/s", group: "gyro", min: 0.0, max: 800.0, step: 5.0, precision: 1, defaultVal: 28.5 },
    { key: "gyro_mag_range", label: "gyro_range", name: "Gyro Magnitude Dynamic Range", unit: "°/s", group: "gyro", min: 0.0, max: 800.0, step: 5.0, precision: 1, defaultVal: 24.0 },
    { key: "gyro_mag_rms", label: "gyro_rms", name: "Gyro Magnitude RMS", unit: "°/s", group: "gyro", min: 0.0, max: 600.0, step: 1.0, precision: 1, defaultVal: 26.5 },

    { key: "acc_jerk_mean", label: "SMA", name: "Signal Magnitude Area / Jerk Mean", unit: "g", group: "dynamics", min: 0.0, max: 2.0, step: 0.01, precision: 3, defaultVal: 0.040 },
    { key: "acc_jerk_max", label: "jerk_peak", name: "Peak Jerk Acceleration", unit: "g", group: "dynamics", min: 0.0, max: 10.0, step: 0.05, precision: 2, defaultVal: 0.12 },
    { key: "post_impact_immobility_var", label: "post_impact_var", name: "Post-Impact Immobility Variance", unit: "g²", group: "dynamics", min: 0.0, max: 4.0, step: 0.001, precision: 4, defaultVal: 0.0080 },
    { key: "tilt_differential", label: "tilt_angle_change", name: "Tilt / Orientation Differential", unit: "°", group: "dynamics", min: 0.0, max: 180.0, step: 0.5, precision: 1, defaultVal: 5.8 },
    { key: "peak_acc_jerk_dt", label: "peak_acc_jerk_dt", name: "Peak Jerk Rate (d|a|/dt)", unit: "g/s", group: "dynamics", min: 0.0, max: 300.0, step: 1.0, precision: 1, defaultVal: 6.5 }
  ];

  // Presets
  const PRESETS = {
    walking: {
      id: "walking",
      name: "Normal Activity (ADL / Walking)",
      description: "Nominal baseline movement with low jerk, low angular velocity, nominal 1.0g gravity, and tilt variance < 10°.",
      features: [1.01, 0.08, 0.88, 1.15, 0.27, 1.01, 22.4, 14.2, 4.5, 28.5, 24.0, 26.5, 0.040, 0.12, 0.0080, 5.8, 6.5],
      immobilitySecs: 10,
      isMotionless: false // Walking continues naturally
    },
    hard_fall: {
      id: "hard_fall",
      name: "Hard Ground Fall (Critical Incident)",
      description: "Violent rotational descent: free-fall drop (<0.2g), high shock impact spike (4.25g), high gyro rotation (>380°/s), drastic 75° tilt shift, followed by motionless resting on the ground.",
      features: [1.25, 0.88, 0.12, 4.25, 4.13, 1.53, 115.0, 92.5, 3.8, 385.0, 381.2, 147.6, 0.280, 2.15, 0.0007, 75.3, 98.4],
      immobilitySecs: 10,
      isMotionless: true // Resting motionless on ground
    }
  };

  /* =========================================================================
     TRANSPILED C99 DECISION TREES (Exact Parity with fall_trees.c)
     ========================================================================= */

  /* Decision Tree #0 (Nodes: 59) */
  function tree_0_predict(x) {
    if (x[16] <= 26.43838882) {
      if (x[3] <= 1.06531674) {
        if (x[4] <= 0.45513150) {
          if (x[14] <= 0.00159361) {
            if (x[3] <= 0.99247280) return 0.14705882;
            else return 0.0;
          } else {
            if (x[14] <= 0.00164796) return 1.0;
            else return 0.0;
          }
        } else {
          if (x[15] <= 4.84027374) return 0.0;
          else return 1.0;
        }
      } else {
        if (x[2] <= 0.86447012) {
          if (x[8] <= 5.28023505) {
            if (x[13] <= 0.31316878) return 0.30811808;
            else return 0.68367347;
          } else {
            if (x[15] <= 16.97944927) return 0.05147059;
            else return 0.26876513;
          }
        } else {
          if (x[16] <= 5.40114665) {
            if (x[9] <= 12.87999868) return 0.0;
            else return 0.37193764;
          } else {
            if (x[14] <= 0.00074126) return 0.90909091;
            else return 0.62430939;
          }
        }
      }
    } else {
      if (x[14] <= 0.43962942) {
        if (x[15] <= 23.35775661) {
          if (x[7] <= 68.33814621) {
            if (x[4] <= 8.56427479) return 0.05882353;
            else return 1.0;
          } else {
            if (x[6] <= 151.20217133) return 0.90666667;
            else return 0.0;
          }
        } else {
          if (x[10] <= 282.79219055) {
            if (x[8] <= 3.77847278) return 0.89583333;
            else return 0.42857143;
          } else {
            if (x[13] <= 0.57228363) return 0.0;
            else return 0.97058824;
          }
        }
      } else {
        if (x[3] <= 6.07938719) {
          if (x[7] <= 80.83878708) {
            if (x[2] <= 0.22991604) return 0.0;
            else return 0.46534653;
          } else {
            if (x[12] <= 0.23969354) return 0.88118812;
            else return 0.0;
          }
        } else {
          if (x[7] <= 69.61840820) {
            if (x[0] <= 1.32554710) return 1.0;
            else return 0.0;
          } else {
            if (x[8] <= 33.52669430) return 0.98648649;
            else return 0.0;
          }
        }
      }
    }
  }

  /* Decision Tree #1 (Nodes: 45) */
  function tree_1_predict(x) {
    if (x[9] <= 306.72827148) {
      if (x[10] <= 12.62828541) {
        return 0.0;
      } else {
        if (x[2] <= 0.58147442) {
          if (x[1] <= 0.20591887) {
            if (x[9] <= 263.94604492) return 0.84239130;
            else return 0.23076923;
          } else {
            if (x[8] <= 3.77847278) return 0.68085106;
            else return 0.15277778;
          }
        } else {
          if (x[9] <= 105.58447266) {
            if (x[0] <= 1.01932102) return 0.18508287;
            else return 0.56052632;
          } else {
            if (x[14] <= 0.00079769) return 0.51401869;
            else return 0.19068935;
          }
        }
      }
    } else {
      if (x[3] <= 2.20189726) {
        if (x[15] <= 98.17837143) {
          if (x[16] <= 28.98400116) {
            if (x[11] <= 145.31367493) return 0.58888889;
            else return 0.07792208;
          } else {
            if (x[15] <= 20.62915611) return 0.0;
            else return 0.94202899;
          }
        } else {
          if (x[13] <= 0.57421580) return 0.0;
          else return 1.0;
        }
      } else {
        if (x[9] <= 358.50306702) {
          if (x[15] <= 15.12270594) {
            if (x[12] <= 0.19298422) return 0.61111111;
            else return 0.0;
          } else {
            if (x[2] <= 0.16230039) return 0.29729730;
            else return 0.85280374;
          }
        } else {
          if (x[13] <= 0.61044216) {
            if (x[4] <= 1.85128266) return 1.0;
            else return 0.0;
          } else {
            if (x[7] <= 90.95869446) return 0.79720280;
            else return 0.97419355;
          }
        }
      }
    }
  }

  /* Decision Tree #2 (Nodes: 47) */
  function tree_2_predict(x) {
    if (x[10] <= 305.45742798) {
      if (x[10] <= 12.67460966) {
        return 0.0;
      } else {
        if (x[2] <= 0.53160235) {
          if (x[14] <= 0.27348220) {
            if (x[12] <= 0.03591193) return 0.93069307;
            else return 0.57967033;
          } else {
            if (x[12] <= 0.11361329) return 0.55555556;
            else return 0.05855856;
          }
        } else {
          if (x[1] <= 0.10068190) {
            if (x[5] <= 1.01934099) return 0.17785844;
            else return 0.49373882;
          } else {
            if (x[4] <= 1.10159725) return 0.15521327;
            else return 0.39662447;
          }
        }
      }
    } else {
      if (x[13] <= 0.57979915) {
        if (x[3] <= 2.20394397) {
          if (x[5] <= 1.09512049) {
            if (x[7] <= 94.72222137) return 0.54929577;
            else return 0.0;
          } else return 0.0;
        } else {
          if (x[13] <= 0.38993828) {
            if (x[1] <= 0.41547866) return 1.0;
            else return 0.0;
          } else {
            if (x[3] <= 2.38304269) return 0.0;
            else return 0.78260870;
          }
        }
      } else {
        if (x[8] <= 22.33417130) {
          if (x[9] <= 361.06028748) {
            if (x[2] <= 0.16713607) return 0.32432432;
            else return 0.88809524;
          } else {
            if (x[7] <= 86.36565399) return 0.84705882;
            else return 0.98037383;
          }
        } else {
          if (x[0] <= 1.42527330) {
            if (x[9] <= 349.89788818) return 0.0;
            else return 0.90322581;
          } else {
            if (x[7] <= 91.10566711) return 0.0;
            else return 0.80487805;
          }
        }
      }
    }
  }

  /* Decision Tree #3 (Nodes: 49) */
  function tree_3_predict(x) {
    if (x[10] <= 295.35235596) {
      if (x[15] <= 15.81352043) {
        if (x[7] <= 2.35825646) return 0.0;
        else {
          if (x[8] <= 7.39809275) {
            if (x[3] <= 1.06526262) return 0.07792208;
            else return 0.34615385;
          } else {
            if (x[7] <= 72.71885300) return 0.01521739;
            else return 0.38709677;
          }
        }
      } else {
        if (x[12] <= 0.23218720) {
          if (x[6] <= 52.89739799) {
            if (x[2] <= 0.68278742) return 0.85106383;
            else return 0.55133080;
          } else {
            if (x[0] <= 1.10386586) return 0.21869159;
            else return 0.69182390;
          }
        } else {
          if (x[13] <= 3.04411304) return 0.0;
          else {
            if (x[10] <= 254.74796295) return 0.0;
            else return 0.68750000;
          }
        }
      }
    } else {
      if (x[13] <= 0.57979915) {
        if (x[15] <= 79.60025024) {
          if (x[14] <= 0.12062661) {
            if (x[16] <= 17.50049210) return 0.41489362;
            else return 0.10204082;
          } else {
            if (x[3] <= 1.93085545) return 0.0;
            else return 0.89552239;
          }
        } else return 0.0;
      } else {
        if (x[10] <= 340.43287659) {
          if (x[15] <= 15.10367727) {
            if (x[14] <= 2.40809441) return 0.0;
            else return 1.0;
          } else {
            if (x[11] <= 150.28937531) return 0.87636364;
            else return 0.52212389;
          }
        } else {
          if (x[2] <= 0.11231342) {
            if (x[10] <= 360.86773682) return 0.0;
            else return 0.78260870;
          } else {
            if (x[7] <= 89.32266235) return 0.85937500;
            else return 0.98404702;
          }
        }
      }
    }
  }

  /* Decision Tree #4 (Nodes: 53) */
  function tree_4_predict(x) {
    if (x[4] <= 1.36348265) {
      if (x[15] <= 5.77273560) {
        if (x[9] <= 12.98623562) return 0.0;
        else {
          if (x[10] <= 158.00846100) {
            if (x[9] <= 13.08078575) return 1.0;
            else return 0.21314387;
          } else return 0.0;
        }
      } else {
        if (x[14] <= 0.00306503) {
          if (x[0] <= 1.04180449) {
            if (x[13] <= 0.07351335) return 0.03921569;
            else return 0.54804270;
          } else {
            if (x[10] <= 100.08364868) return 0.91919192;
            else return 0.59000000;
          }
        } else {
          if (x[14] <= 0.01380143) {
            if (x[0] <= 0.95208973) return 0.84000000;
            else return 0.11267606;
          } else {
            if (x[6] <= 102.21152115) return 0.44444444;
            else return 0.17241379;
          }
        }
      }
    } else {
      if (x[8] <= 13.24616337) {
        if (x[13] <= 0.90751332) {
          if (x[9] <= 308.01777649) {
            if (x[1] <= 0.30928057) return 0.67460317;
            else return 0.0;
          } else {
            if (x[9] <= 386.13587952) return 0.86470588;
            else return 0.21428571;
          }
        } else {
          if (x[15] <= 23.37732315) {
            if (x[7] <= 76.93921661) return 0.15151515;
            else return 0.96000000;
          } else {
            if (x[9] <= 280.69839478) return 0.69117647;
            else return 0.96394850;
          }
        }
      } else {
        if (x[10] <= 341.18510437) {
          if (x[6] <= 101.27658463) return 0.0;
          else {
            if (x[12] <= 0.25134580) return 0.56967213;
            else return 0.04444444;
          }
        } else {
          if (x[12] <= 0.44140145) {
            if (x[15] <= 28.71224880) return 0.50000000;
            else return 0.93471810;
          } else {
            if (x[1] <= 1.61336464) return 0.0;
            else return 1.0;
          }
        }
      }
    }
  }

  /* Decision Tree #5 (Nodes: 53) */
  function tree_5_predict(x) {
    if (x[15] <= 23.36272812) {
      if (x[16] <= 144.00727844) {
        if (x[4] <= 0.10745712) {
          if (x[10] <= 55.63854027) {
            if (x[6] <= 8.20740080) return 0.00621118;
            else return 0.20512821;
          } else {
            if (x[8] <= 1.88902062) return 0.0;
            else return 0.94736842;
          }
        } else {
          if (x[8] <= 5.54823542) {
            if (x[5] <= 1.04683143) return 0.27868852;
            else return 0.56741573;
          } else {
            if (x[2] <= 0.91624367) return 0.11583012;
            else return 0.77777778;
          }
        }
      } else {
        if (x[8] <= 24.66504478) {
          if (x[4] <= 5.79901743) return 0.0;
          else {
            if (x[10] <= 245.87073517) return 0.87500000;
            else return 1.0;
          }
        } else return 0.0;
      }
    } else {
      if (x[16] <= 28.69723892) {
        if (x[14] <= 0.13872445) {
          if (x[6] <= 56.95277405) {
            if (x[13] <= 0.31931037) return 0.40588235;
            else return 0.92473118;
          } else {
            if (x[6] <= 84.94792557) return 0.30612245;
            else return 0.10064935;
          }
        } else {
          if (x[5] <= 1.14345884) return 0.0;
          else {
            if (x[1] <= 0.42604493) return 0.98333333;
            else return 0.0;
          }
        }
      } else {
        if (x[8] <= 17.56175423) {
          if (x[9] <= 285.12757874) {
            if (x[2] <= 0.19232079) return 0.13333333;
            else return 0.75182482;
          } else {
            if (x[9] <= 344.12226868) return 0.86800000;
            else return 0.97383029;
          }
        } else {
          if (x[15] <= 44.93946457) {
            if (x[4] <= 3.32263577) return 0.67213115;
            else return 0.20779221;
          } else {
            if (x[11] <= 185.11415863) return 0.64285714;
            else return 0.94444444;
          }
        }
      }
    }
  }

  /* Decision Tree #6 (Nodes: 55) */
  function tree_6_predict(x) {
    if (x[16] <= 28.69723892) {
      if (x[15] <= 6.95152545) {
        if (x[9] <= 12.98623562) return 0.0;
        else {
          if (x[6] <= 35.79763794) {
            if (x[5] <= 1.05874825) return 0.23503326;
            else return 0.70000000;
          } else {
            if (x[8] <= 0.79202345) return 0.66666667;
            else return 0.0;
          }
        }
      } else {
        if (x[14] <= 0.00247374) {
          if (x[11] <= 67.51885605) {
            if (x[3] <= 1.15453649) return 0.36065574;
            else return 0.74889868;
          } else {
            if (x[4] <= 0.83069199) return 0.06451613;
            else return 0.60000000;
          }
        } else {
          if (x[0] <= 0.95170286) {
            if (x[10] <= 244.67092133) return 0.97916667;
            else return 0.0;
          } else {
            if (x[3] <= 2.11888874) return 0.20744186;
            else return 0.60377358;
          }
        }
      }
    } else {
      if (x[15] <= 23.37732315) {
        if (x[4] <= 6.06063294) {
          if (x[12] <= 0.15675484) {
            if (x[14] <= 1.25451064) return 0.36170213;
            else return 1.0;
          } else {
            if (x[7] <= 76.55558014) return 0.0;
            else return 0.04000000;
          }
        } else {
          if (x[7] <= 58.99475098) return 0.0;
          else {
            if (x[15] <= 18.34903812) return 0.98850575;
            else return 0.66666667;
          }
        }
      } else {
        if (x[7] <= 87.61261368) {
          if (x[14] <= 0.51679671) {
            if (x[6] <= 97.56238556) return 0.89877301;
            else return 0.68773234;
          } else {
            if (x[14] <= 2.33298314) return 0.17605634;
            else return 0.90000000;
          }
        } else {
          if (x[1] <= 0.40514290) {
            if (x[13] <= 1.14996296) return 0.88764045;
            else return 0.28571429;
          } else {
            if (x[2] <= 0.11978846) return 0.85714286;
            else return 0.98127036;
          }
        }
      }
    }
  }

  /* Decision Tree #7 (Nodes: 55) */
  function tree_7_predict(x) {
    if (x[16] <= 28.69723892) {
      if (x[11] <= 7.97030807) {
        if (x[3] <= 1.13287109) {
          if (x[5] <= 1.03137946) return 0.0;
          else {
            if (x[0] <= 1.03134805) return 1.0;
            else return 0.0;
          }
        } else return 1.0;
      } else {
        if (x[12] <= 0.03520293) {
          if (x[13] <= 0.31094192) {
            if (x[5] <= 1.06059611) return 0.26447710;
            else return 0.55111111;
          } else {
            if (x[2] <= 0.82277167) return 0.80327869;
            else return 0.0;
          }
        } else {
          if (x[8] <= 4.72504807) {
            if (x[8] <= 4.70634079) return 0.31981982;
            else return 1.0;
          } else {
            if (x[7] <= 54.32629585) return 0.03759398;
            else return 0.23076923;
          }
        }
      }
    } else {
      if (x[7] <= 68.29434586) {
        if (x[5] <= 1.27746868) {
          if (x[15] <= 41.84893417) {
            if (x[13] <= 0.57604215) return 1.0;
            else return 0.24675325;
          } else {
            if (x[6] <= 59.01062012) return 0.98529412;
            else return 0.47727273;
          }
        } else {
          if (x[14] <= 0.35382774) {
            if (x[0] <= 1.23445916) return 0.0;
            else return 0.75000000;
          } else {
            if (x[12] <= 0.18696183) return 0.81481481;
            else return 0.0;
          }
        }
      } else {
        if (x[10] <= 341.14585876) {
          if (x[3] <= 6.07938719) {
            if (x[11] <= 149.16445923) return 0.83185841;
            else return 0.39370079;
          } else {
            if (x[11] <= 161.19084167) return 0.98412698;
            else return 0.0;
          }
        } else {
          if (x[14] <= 0.91735685) {
            if (x[15] <= 8.65284109) return 0.66666667;
            else return 0.97266881;
          } else {
            if (x[3] <= 6.33884382) return 0.45454545;
            else return 0.98882682;
          }
        }
      }
    }
  }

  /* Decision Tree #8 (Nodes: 51) */
  function tree_8_predict(x) {
    if (x[15] <= 23.51630592) {
      if (x[4] <= 6.62071919) {
        if (x[7] <= 2.36980331) return 0.0;
        else {
          if (x[8] <= 6.30209160) {
            if (x[5] <= 0.94135651) return 1.0;
            else return 0.28707375;
          } else {
            if (x[14] <= 2.96814132) return 0.11486486;
            else return 1.0;
          }
        }
      } else {
        if (x[12] <= 0.45092742) {
          if (x[13] <= 2.76386249) {
            if (x[2] <= 0.58956620) return 1.0;
            else return 0.0;
          } else {
            if (x[1] <= 1.03977287) return 0.92307692;
            else return 1.0;
          }
        } else return 0.0;
      }
    } else {
      if (x[9] <= 338.26483154) {
        if (x[16] <= 16.28131390) {
          if (x[8] <= 1.27433902) {
            if (x[3] <= 1.23132515) return 0.87755102;
            else return 0.19047619;
          } else {
            if (x[5] <= 0.97930613) return 0.76666667;
            else return 0.21052632;
          }
        } else {
          if (x[11] <= 90.62574768) {
            if (x[12] <= 0.21980829) return 0.88301887;
            else return 0.0;
          } else {
            if (x[16] <= 29.30069923) return 0.19000000;
            else return 0.56435644;
          }
        }
      } else {
        if (x[12] <= 0.07559978) {
          if (x[15] <= 95.90960312) {
            if (x[5] <= 1.05746913) return 0.94545455;
            else return 0.35714286;
          } else {
            if (x[16] <= 28.71079063) return 0.0;
            else return 0.50000000;
          }
        } else {
          if (x[7] <= 82.41355515) {
            if (x[1] <= 1.09185290) return 0.78048780;
            else return 0.35555556;
          } else {
            if (x[4] <= 1.24528462) return 0.0;
            else return 0.97210300;
          }
        }
      }
    }
  }

  /* Decision Tree #9 (Nodes: 37) */
  function tree_9_predict(x) {
    if (x[13] <= 0.57382569) {
      if (x[7] <= 2.38266826) return 0.0;
      else {
        if (x[5] <= 0.95008212) {
          if (x[5] <= 0.91589054) return 0.0;
          else {
            if (x[13] <= 0.07361323) return 0.0;
            else return 0.94915254;
          }
        } else {
          if (x[3] <= 1.06548858) {
            if (x[4] <= 0.05473782) return 0.41176471;
            else return 0.01345291;
          } else {
            if (x[1] <= 0.09436682) return 0.39390519;
            else return 0.22536287;
          }
        }
      }
    } else {
      if (x[2] <= 0.10143208) {
        if (x[7] <= 106.23439789) {
          if (x[11] <= 124.47579193) return 0.0;
          else {
            if (x[1] <= 0.41053334) return 1.0;
            else return 0.0;
          }
        } else return 1.0;
      } else {
        if (x[10] <= 340.83998108) {
          if (x[12] <= 0.30027568) {
            if (x[4] <= 4.07429719) return 0.55809129;
            else return 0.94423792;
          } else return 0.0;
        } else {
          if (x[14] <= 0.43650019) {
            if (x[10] <= 359.36767578) return 0.94594595;
            else return 0.98579235;
          } else {
            if (x[8] <= 29.51489162) return 0.91097923;
            else return 0.0;
          }
        }
      }
    }
  }

  /* Decision Tree #10 (Nodes: 55) */
  function tree_10_predict(x) {
    if (x[3] <= 2.11716509) {
      if (x[15] <= 9.29778290) {
        if (x[10] <= 12.62969017) return 0.0;
        else {
          if (x[4] <= 0.46402308) {
            if (x[13] <= 0.05297239) return 0.19881306;
            else return 0.40127389;
          } else {
            if (x[0] <= 0.92889965) return 0.88888889;
            else return 0.03804348;
          }
        }
      } else {
        if (x[6] <= 38.28432846) {
          if (x[12] <= 0.01185625) {
            if (x[14] <= 0.00047566) return 0.54545455;
            else return 0.0;
          } else {
            if (x[16] <= 6.45835471) return 0.41732283;
            else return 0.78902954;
          }
        } else {
          if (x[13] <= 0.31376323) {
            if (x[0] <= 0.95152143) return 0.87500000;
            else return 0.16877637;
          } else {
            if (x[1] <= 0.23019889) return 0.58823529;
            else return 0.13829787;
          }
        }
      }
    } else {
      if (x[10] <= 279.26496887) {
        if (x[8] <= 4.95914507) {
          if (x[0] <= 1.04776269) return 0.0;
          else {
            if (x[15] <= 23.98679638) return 0.37500000;
            else return 0.93023256;
          }
        } else {
          if (x[7] <= 69.25072479) {
            if (x[11] <= 197.01306152) return 0.01327434;
            else return 1.0;
          } else {
            if (x[15] <= 45.42498207) return 0.0;
            else return 0.72222222;
          }
        }
      } else {
        if (x[15] <= 23.32290077) {
          if (x[6] <= 124.04182434) {
            if (x[15] <= 9.94642830) return 0.50000000;
            else return 0.86725664;
          } else {
            if (x[13] <= 2.88014555) return 0.0;
            else return 0.71428571;
          }
        } else {
          if (x[13] <= 0.57979915) {
            if (x[11] <= 99.51582336) return 1.0;
            else return 0.16666667;
          } else {
            if (x[8] <= 21.16277695) return 0.95918367;
            else return 0.78682171;
          }
        }
      }
    }
  }

  /* Decision Tree #11 (Nodes: 53) */
  function tree_11_predict(x) {
    if (x[7] <= 73.67254257) {
      if (x[10] <= 24.67707443) {
        if (x[0] <= 1.07069838) {
          if (x[16] <= 2.99133968) {
            if (x[4] <= 0.17184385) return 0.00619835;
            else return 0.36363636;
          } else {
            if (x[4] <= 0.10358592) return 0.77777778;
            else return 0.0;
          }
        } else {
          if (x[10] <= 12.02006531) return 0.0;
          else {
            if (x[15] <= 0.71428809) return 0.0;
            else return 0.92307692;
          }
        }
      } else {
        if (x[10] <= 277.91323853) {
          if (x[11] <= 90.73412323) {
            if (x[7] <= 48.84062576) return 0.34480287;
            else return 0.67300380;
          } else {
            if (x[14] <= 0.05430488) return 0.04301075;
            else return 0.25680934;
          }
        } else {
          if (x[11] <= 126.81624222) {
            if (x[16] <= 8.62019396) return 0.0;
            else return 0.85082873;
          } else {
            if (x[14] <= 0.63867459) return 0.58108108;
            else return 0.07407407;
          }
        }
      }
    } else {
      if (x[16] <= 25.59570122) {
        if (x[15] <= 72.85099411) {
          if (x[8] <= 3.32495320) {
            if (x[12] <= 0.04360824) return 0.78947368;
            else return 0.25000000;
          } else {
            if (x[14] <= 0.17133712) return 0.17592593;
            else return 0.66666667;
          }
        } else return 0.0;
      } else {
        if (x[2] <= 0.10576385) {
          if (x[4] <= 5.58161497) {
            if (x[12] <= 0.08237608) return 1.0;
            else return 0.0;
          } else return 1.0;
        } else {
          if (x[7] <= 87.71795654) {
            if (x[8] <= 16.13426971) return 0.88087774;
            else return 0.56934307;
          } else {
            if (x[13] <= 0.57979915) return 0.42857143;
            else return 0.96992481;
          }
        }
      }
    }
  }

  /* Decision Tree #12 (Nodes: 51) */
  function tree_12_predict(x) {
    if (x[13] <= 0.57394478) {
      if (x[7] <= 5.82621121) {
        if (x[16] <= 2.86296427) {
          if (x[9] <= 15.58454180) return 0.0;
          else {
            if (x[15] <= 1.12503266) return 0.34090909;
            else return 0.0;
          }
        } else {
          if (x[0] <= 1.06940651) {
            if (x[2] <= 0.97205532) return 0.0;
            else return 1.0;
          } else return 1.0;
        }
      } else {
        if (x[15] <= 81.19392776) {
          if (x[2] <= 0.96333000) {
            if (x[9] <= 47.22297287) return 0.0;
            else return 0.31193124;
          } else {
            if (x[11] <= 19.89568710) return 0.77570093;
            else return 0.44927536;
          }
        } else {
          if (x[5] <= 1.15691042) return 0.0;
          else {
            if (x[16] <= 12.46688604) return 1.0;
            else return 0.0;
          }
        }
      }
    } else {
      if (x[15] <= 23.35775661) {
        if (x[4] <= 6.06063294) {
          if (x[12] <= 0.19543710) {
            if (x[9] <= 254.12702179) return 0.0;
            else return 0.61956522;
          } else return 0.0;
        } else {
          if (x[8] <= 23.82715416) {
            if (x[4] <= 6.62071919) return 0.63636364;
            else return 1.0;
          } else return 0.0;
        }
      } else {
        if (x[3] <= 5.31530595) {
          if (x[14] <= 0.65722343) {
            if (x[12] <= 0.27656715) return 0.88692946;
            else return 0.0;
          } else {
            if (x[11] <= 196.43041992) return 0.23188406;
            else return 0.77272727;
          }
        } else {
          if (x[12] <= 0.46188742) {
            if (x[10] <= 283.03623962) return 0.70588235;
            else return 0.97624434;
          } else return 0.0;
        }
      }
    }
  }

  /* Decision Tree #13 (Nodes: 51) */
  function tree_13_predict(x) {
    if (x[10] <= 305.45742798) {
      if (x[13] <= 0.31359340) {
        if (x[5] <= 0.94376287) {
          if (x[2] <= 0.70562556) return 1.0;
          else return 0.0;
        } else {
          if (x[10] <= 12.67460966) return 0.0;
          else {
            if (x[6] <= 52.91826439) return 0.33067376;
            else return 0.12800000;
          }
        }
      } else {
        if (x[6] <= 52.74117661) {
          if (x[9] <= 155.66015625) {
            if (x[13] <= 0.50772268) return 0.61764706;
            else return 0.0;
          } else {
            if (x[7] <= 28.35586071) return 0.0;
            else return 0.89719626;
          }
        } else {
          if (x[7] <= 73.89125824) {
            if (x[3] <= 5.12363482) return 0.15904139;
            else return 0.63291139;
          } else {
            if (x[6] <= 101.52157593) return 0.83838384;
            else return 0.36986301;
          }
        }
      }
    } else {
      if (x[1] <= 0.30118570) {
        if (x[9] <= 323.03376770) {
          if (x[15] <= 67.93005371) {
            if (x[15] <= 23.84099865) return 0.0;
            else return 0.92857143;
          } else return 0.0;
        } else {
          if (x[16] <= 28.73179817) {
            if (x[1] <= 0.10940101) return 0.61538462;
            else return 0.14615385;
          } else {
            if (x[0] <= 1.08663279) return 1.0;
            else return 0.0;
          }
        }
      } else {
        if (x[16] <= 24.72169399) {
          if (x[7] <= 86.49647903) return 1.0;
          else return 0.0;
        } else {
          if (x[2] <= 0.11231342) {
            if (x[4] <= 5.64931273) return 0.07142857;
            else return 0.93333333;
          } else {
            if (x[12] <= 0.46591842) return 0.92984474;
            else return 0.0;
          }
        }
      }
    }
  }

  /* Decision Tree #14 (Nodes: 61) */
  function tree_14_predict(x) {
    if (x[5] <= 1.13515037) {
      if (x[15] <= 6.95152545) {
        if (x[0] <= 1.06021434) {
          if (x[0] <= 0.92928454) {
            if (x[5] <= 0.76959640) return 0.0;
            else return 1.0;
          } else {
            if (x[7] <= 2.37150180) return 0.0;
            else return 0.11177347;
          }
        } else {
          if (x[10] <= 111.47418594) {
            if (x[10] <= 12.50379610) return 0.0;
            else return 0.86301370;
          } else return 0.0;
        }
      } else {
        if (x[10] <= 293.42012024) {
          if (x[8] <= 3.38286602) {
            if (x[16] <= 15.61486483) return 0.46055437;
            else return 0.73195876;
          } else {
            if (x[5] <= 0.94403258) return 0.95652174;
            else return 0.18775510;
          }
        } else {
          if (x[16] <= 27.10385132) {
            if (x[11] <= 118.79768753) return 0.71875000;
            else return 0.16535433;
          } else {
            if (x[11] <= 252.52431488) return 0.94267516;
            else return 0.0;
          }
        }
      }
    } else {
      if (x[15] <= 23.33375263) {
        if (x[4] <= 5.79901743) {
          if (x[15] <= 19.28867435) {
            if (x[4] <= 3.82991719) return 0.0;
            else return 0.22368421;
          } else {
            if (x[5] <= 1.27305061) return 0.82051282;
            else return 0.0;
          }
        } else {
          if (x[12] <= 0.37614910) {
            if (x[4] <= 6.61831594) return 0.81818182;
            else return 1.0;
          } else {
            if (x[2] <= 0.48889013) return 1.0;
            else return 0.0;
          }
        }
      } else {
        if (x[7] <= 83.02468109) {
          if (x[14] <= 0.53909522) {
            if (x[11] <= 147.89490509) return 0.82641509;
            else return 0.53773585;
          } else {
            if (x[3] <= 9.02336979) return 0.15315315;
            else return 1.0;
          }
        } else {
          if (x[4] <= 2.23120332) {
            if (x[14] <= 0.18453901) return 0.52830189;
            else return 0.92000000;
          } else {
            if (x[12] <= 0.51364422) return 0.96118560;
            else return 0.0;
          }
        }
      }
    }
  }

  /* Decision Tree #15 (Nodes: 61) */
  function tree_15_predict(x) {
    if (x[9] <= 306.74008179) {
      if (x[10] <= 45.23978424) {
        if (x[4] <= 0.10848216) {
          if (x[5] <= 0.95880854) {
            if (x[6] <= 7.94323111) return 0.0;
            else return 0.88888889;
          } else {
            if (x[16] <= 2.99133968) return 0.0;
            else return 0.46153846;
          }
        } else {
          if (x[13] <= 0.03488082) {
            if (x[5] <= 1.03564841) return 0.0;
            else return 1.0;
          } else {
            if (x[4] <= 0.12806612) return 0.0;
            else return 0.18691589;
          }
        }
      } else {
        if (x[8] <= 4.21110487) {
          if (x[16] <= 15.64397144) {
            if (x[11] <= 24.70267010) return 0.62926829;
            else return 0.32339089;
          } else {
            if (x[2] <= 0.09788831) return 0.0;
            else return 0.78402367;
          }
        } else {
          if (x[0] <= 0.94878674) {
            if (x[1] <= 0.09951565) return 0.0;
            else return 0.89285714;
          } else {
            if (x[4] <= 0.15984304) return 0.87500000;
            else return 0.13993174;
          }
        }
      }
    } else {
      if (x[1] <= 0.30775762) {
        if (x[14] <= 0.10202105) {
          if (x[11] <= 123.37654877) {
            if (x[16] <= 6.56733871) return 0.0;
            else return 0.80555556;
          } else {
            if (x[16] <= 32.58225060) return 0.11976048;
            else return 0.89473684;
          }
        } else {
          if (x[13] <= 0.38401888) return 0.0;
          else {
            if (x[14] <= 0.19146107) return 1.0;
            else return 0.0;
          }
        }
      } else {
        if (x[7] <= 88.33454895) {
          if (x[14] <= 0.63526767) {
            if (x[4] <= 3.63789344) return 0.67455621;
            else return 0.92929293;
          } else {
            if (x[16] <= 150.51702881) return 0.23076923;
            else return 0.83561644;
          }
        } else {
          if (x[1] <= 0.43179277) {
            if (x[12] <= 0.07559978) return 0.28571429;
            else return 0.87096774;
          } else {
            if (x[8] <= 47.11879921) return 0.96875000;
            else return 0.28571429;
          }
        }
      }
    }
  }

  /* Decision Tree #16 (Nodes: 41) */
  function tree_16_predict(x) {
    if (x[10] <= 298.20016479) {
      if (x[10] <= 12.67460966) return 0.0;
      else {
        if (x[14] <= 0.34124291) {
          if (x[3] <= 2.91837204) {
            if (x[2] <= 0.53160235) return 0.59337349;
            else return 0.28778368;
          } else {
            if (x[8] <= 4.97454953) return 0.98684211;
            else return 0.61333333;
          }
        } else {
          if (x[4] <= 8.56647205) {
            if (x[12] <= 0.11396768) return 0.60000000;
            else return 0.01369863;
          } else return 1.0;
        }
      }
    } else {
      if (x[13] <= 0.58495951) {
        if (x[12] <= 0.02910120) return 0.0;
        else {
          if (x[11] <= 119.93266296) {
            if (x[8] <= 2.07889950) return 0.0;
            else return 0.84126984;
          } else {
            if (x[15] <= 72.75048447) return 0.37878788;
            else return 0.0;
          }
        }
      } else {
        if (x[9] <= 363.08099365) {
          if (x[6] <= 163.62315369) {
            if (x[2] <= 0.16713607) return 0.20588235;
            else return 0.90019569;
          } else {
            if (x[15] <= 97.82343292) return 0.05714286;
            else return 1.0;
          }
        } else {
          if (x[8] <= 17.55042934) {
            if (x[7] <= 90.97215271) return 0.92436975;
            else return 0.98753894;
          } else {
            if (x[12] <= 0.33385621) return 0.85204082;
            else return 0.34090909;
          }
        }
      }
    }
  }

  /* Decision Tree #17 (Nodes: 55) */
  function tree_17_predict(x) {
    if (x[16] <= 29.24797535) {
      if (x[15] <= 7.52893901) {
        if (x[10] <= 12.67460966) return 0.0;
        else {
          if (x[2] <= 0.95331997) {
            if (x[5] <= 0.93748495) return 0.83333333;
            else return 0.11974110;
          } else {
            if (x[10] <= 36.44309425) return 0.26811594;
            else return 0.68750000;
          }
        }
      } else {
        if (x[5] <= 0.94973135) {
          if (x[0] <= 0.74050149) return 0.0;
          else {
            if (x[12] <= 0.01800926) return 0.0;
            else return 1.0;
          }
        } else {
          if (x[14] <= 0.00305908) {
            if (x[13] <= 0.12255964) return 0.38235294;
            else return 0.66108787;
          } else {
            if (x[14] <= 0.09556469) return 0.19877049;
            else return 0.65185185;
          }
        }
      }
    } else {
      if (x[14] <= 0.32106738) {
        if (x[7] <= 68.97006607) {
          if (x[15] <= 41.29341888) {
            if (x[11] <= 67.28036499) return 0.84615385;
            else return 0.17924528;
          } else {
            if (x[8] <= 5.54386783) return 0.95774648;
            else return 0.55555556;
          }
        } else {
          if (x[9] <= 298.88163757) {
            if (x[11] <= 113.68330002) return 0.88888889;
            else return 0.30000000;
          } else {
            if (x[4] <= 3.64842570) return 0.92156863;
            else return 0.98104265;
          }
        }
      } else {
        if (x[4] <= 5.73863983) {
          if (x[12] <= 0.16866154) {
            if (x[1] <= 0.33168741) return 0.0;
            else return 0.80487805;
          } else {
            if (x[10] <= 371.92608643) return 0.06250000;
            else return 0.76190476;
          }
        } else {
          if (x[7] <= 69.61840820) {
            if (x[12] <= 0.29396028) return 0.91666667;
            else return 0.0;
          } else {
            if (x[12] <= 0.49300024) return 0.97560976;
            else return 0.0;
          }
        }
      }
    }
  }

  /* Decision Tree #18 (Nodes: 57) */
  function tree_18_predict(x) {
    if (x[13] <= 0.57394478) {
      if (x[9] <= 29.40414429) {
        if (x[0] <= 1.07069838) {
          if (x[4] <= 0.17548890) {
            if (x[5] <= 0.95880854) return 0.15384615;
            else return 0.00872600;
          } else {
            if (x[4] <= 0.17792762) return 1.0;
            else return 0.0;
          }
        } else {
          if (x[10] <= 12.49374533) return 0.0;
          else {
            if (x[4] <= 0.10853463) return 0.0;
            else return 0.89473684;
          }
        }
      } else {
        if (x[8] <= 5.26000643) {
          if (x[5] <= 0.94009817) {
            if (x[6] <= 61.30211258) return 1.0;
            else return 0.0;
          } else {
            if (x[16] <= 5.62181759) return 0.29779412;
            else return 0.47819972;
          }
        } else {
          if (x[2] <= 0.91624367) {
            if (x[0] <= 0.96594191) return 0.54385965;
            else return 0.12457143;
          } else {
            if (x[1] <= 0.06392646) return 0.25000000;
            else return 0.88888889;
          }
        }
      }
    } else {
      if (x[7] <= 74.55084229) {
        if (x[2] <= 0.27046470) {
          if (x[12] <= 0.10716256) {
            if (x[4] <= 2.29788721) return 1.0;
            else return 0.60869565;
          } else {
            if (x[13] <= 3.10943198) return 0.04739336;
            else return 0.80000000;
          }
        } else {
          if (x[15] <= 20.85357380) {
            if (x[6] <= 48.24652100) return 0.72727273;
            else return 0.02898551;
          } else {
            if (x[6] <= 52.73700523) return 0.98876404;
            else return 0.61507937;
          }
        }
      } else {
        if (x[12] <= 0.42563385) {
          if (x[10] <= 330.97776794) {
            if (x[0] <= 1.41219139) return 0.75000000;
            else return 0.30000000;
          } else {
            if (x[2] <= 0.03547413) return 0.0;
            else return 0.96889477;
          }
        } else {
          if (x[1] <= 1.57329148) return 0.0;
          else return 1.0;
        }
      }
    }
  }

  /* Decision Tree #19 (Nodes: 49) */
  function tree_19_predict(x) {
    if (x[10] <= 279.81806946) {
      if (x[9] <= 12.91326284) return 0.0;
      else {
        if (x[3] <= 1.06533629) {
          if (x[3] <= 0.99340263) {
            if (x[16] <= 0.80992416) return 0.0;
            else return 1.0;
          } else {
            if (x[12] <= 0.01212178) return 0.0;
            else return 0.45454545;
          }
        } else {
          if (x[6] <= 36.86918640) {
            if (x[5] <= 1.06070828) return 0.45193929;
            else return 0.72350230;
          } else {
            if (x[0] <= 0.95170286) return 0.86046512;
            else return 0.19003322;
          }
        }
      }
    } else {
      if (x[13] <= 0.57979915) {
        if (x[16] <= 15.48699522) {
          if (x[11] <= 118.93204498) {
            if (x[14] <= 0.00524819) return 0.77777778;
            else return 0.0;
          } else {
            if (x[3] <= 2.25742269) return 0.06666667;
            else return 0.66666667;
          }
        } else {
          if (x[11] <= 118.76827621) {
            if (x[13] <= 0.46652289) return 0.95833333;
            else return 0.50000000;
          } else {
            if (x[5] <= 1.02538210) return 0.57692308;
            else return 0.11578947;
          }
        }
      } else {
        if (x[8] <= 21.00355148) {
          if (x[7] <= 92.17304993) {
            if (x[6] <= 96.01728058) return 0.97106109;
            else return 0.67441860;
          } else {
            if (x[9] <= 309.18743896) return 0.0;
            else return 0.98001738;
          }
        } else {
          if (x[14] <= 0.29921885) {
            if (x[3] <= 3.06068230) return 0.37037037;
            else return 0.89928058;
          } else {
            if (x[1] <= 0.74928170) return 0.87878788;
            else return 0.34579439;
          }
        }
      }
    }
  }

  /**
   * Evaluates the 20 Random Forest trees and returns the ensemble probability [0.0, 1.0].
   * @param {Array<number>} features 17-element feature array
   * @returns {number} Probability of fall
   */
  function predictRfProbability(features) {
    if (!features || features.length < 17) return 0.0;
    let sum = 0.0;
    sum += tree_0_predict(features);
    sum += tree_1_predict(features);
    sum += tree_2_predict(features);
    sum += tree_3_predict(features);
    sum += tree_4_predict(features);
    sum += tree_5_predict(features);
    sum += tree_6_predict(features);
    sum += tree_7_predict(features);
    sum += tree_8_predict(features);
    sum += tree_9_predict(features);
    sum += tree_10_predict(features);
    sum += tree_11_predict(features);
    sum += tree_12_predict(features);
    sum += tree_13_predict(features);
    sum += tree_14_predict(features);
    sum += tree_15_predict(features);
    sum += tree_16_predict(features);
    sum += tree_17_predict(features);
    sum += tree_18_predict(features);
    sum += tree_19_predict(features);
    return sum / 20.0;
  }

  /**
   * Run Full TinyML & Immobility Two-Tier Diagnostic Pipeline
   * @param {Object} options
   * @param {Array<number>} options.features 17-element array
   * @param {number} options.immobilitySecs Immobility timer (1 to 30s)
   * @param {boolean} options.isMotionless True if subject is resting motionless (SMA < 0.15g)
   */
  function runPipeline(options) {
    const t0 = performance.now();
    const features = options.features || PRESETS.walking.features;
    const immobilitySecs = options.immobilitySecs || 10;
    const isMotionless = (typeof options.isMotionless === 'boolean') ? options.isMotionless : true;

    // Stage 1 Gating: Peak acceleration impact
    const peakAcc = features[3] || 1.0;
    const stage1GatingPassed = peakAcc >= 2.30;

    // Stage 2: Random Forest Decision Ensemble
    const rfProb = predictRfProbability(features);
    const classifierTriggered = rfProb >= 0.50;

    // Stage 3: Post-Fall Immobility Controller
    // Requirements: Primary fall classification AND post-fall movement below dynamic threshold (SMA < 0.15g)
    // for the selected duration (immobilitySecs)
    const smaVal = features[12] || 0.04; // acc_jerk_mean / SMA
    const postImpactVariance = features[14] || 0.008;

    let immobilityVerified = false;
    let finalStatus = "NORMAL";
    let statusDetail = "ADL Verified (Normal Activity)";

    if (classifierTriggered) {
      if (isMotionless) {
        // Confirmed motionless during window: SMA < 0.15g
        immobilityVerified = true;
        finalStatus = "FALL_DETECTED";
        statusDetail = `Critical Incident Confirmed: Shock of ${peakAcc.toFixed(2)}g with ${immobilitySecs}s Immobility`;
      } else {
        // Active motion detected after impact
        immobilityVerified = false;
        finalStatus = "NORMAL";
        statusDetail = "Suppressed: Active voluntary recovery motion detected (False Alarm Blocked)";
      }
    } else {
      finalStatus = "NORMAL";
      statusDetail = "ADL Verified: Nominal activity within safe kinematic boundaries";
    }

    const t1 = performance.now();
    const latencyMs = Number((t1 - t0).toFixed(3));

    // Plaintext output payload
    const payload = {
      status: finalStatus,
      confidence: Number(rfProb.toFixed(4)),
      timestamp: new Date().toISOString(),
      immobility_secs: immobilitySecs,
      peak_impact_g: Number(peakAcc.toFixed(2)),
      sma_g: Number(smaVal.toFixed(3)),
      immobility_verified: immobilityVerified
    };

    return {
      features,
      peakAcc,
      stage1GatingPassed,
      rfProb,
      classifierTriggered,
      immobilitySecs,
      isMotionless,
      immobilityVerified,
      finalStatus,
      statusDetail,
      payload,
      latencyMs
    };
  }

  // Export to global scope
  window.TinyMLEngine = {
    FEATURE_METADATA,
    PRESETS,
    predictRfProbability,
    runPipeline
  };

})(typeof window !== 'undefined' ? window : this);
