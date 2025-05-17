data = [
    {
        "Name": "KD_res8_narrow",
        "Input Dimensions": "1x100x42",
        "Layers": "8",
        "FPS": "3.0",
        "Probability": "-",
        "Dependencies": "-"
    },
    {
        "Name": "ASR_EM_24L",
        "Input Dimensions": "512x128x1",
        "Layers": "120",
        "FPS": "3.0",
        "Probability": "0.2/0.5",
        "Dependencies": "KD_res8_narrow"
    },
    {
        "Name": "SS_HRViT_b1",
        "Input Dimensions": "3x226x226",
        "Layers": "423",
        "FPS": "10.0",
        "Probability": "-",
        "Dependencies": "-"
    },
    {
        "Name": "DE_midas_v21_small",
        "Input Dimensions": "32x98x130",
        "Layers": "92",
        "FPS": "30.0",
        "Probability": "-",
        "Dependencies": "-"
    },
    {
        "Name": "PD_Plane_RCNN_Quarter",
        "Input Dimensions": "3x166x166",
        "Layers": "153",
        "FPS": "15.0",
        "Probability": "-",
        "Dependencies": "-"
    },
    {
        "Name": "OD_D2go_FasterRCNN",
        "Input Dimensions": "3x226x301",
        "Layers": "138",
        "FPS": "10.0/30.0",
        "Probability": "-",
        "Dependencies": "-"
    },
    {
        "Name": "HT_hand_graph_cnn_half",
        "Input Dimensions": "3x134x134",
        "Layers": "224",
        "FPS": "30.0/45.0",
        "Probability": "-",
        "Dependencies": "-"
    },
    {
        "Name": "ES_RITNet",
        "Input Dimensions": "32x162x102",
        "Layers": "42",
        "FPS": "60.0",
        "Probability": "-",
        "Dependencies": "-"
    },
    {
        "Name": "GE_FBNet_C",
        "Input Dimensions": "3x162x102",
        "Layers": "63",
        "FPS": "60.0",
        "Probability": "1.0",
        "Dependencies": "ES_RITNet"
    },
    {
        "Name": "AS_ED_TCN",
        "Input Dimensions": "128x692x1",
        "Layers": "4",
        "FPS": "30.0",
        "Probability": "-",
        "Dependencies": "-"
    },
    {
        "Name": "D2go_FastRCNN",
        "Input Dimensions": "3x226x301",
        "Layers": "138",
        "FPS": "30.0",
        "Probability": "-",
        "Dependencies": "-"
    },
    {
        "Name": "DR_RGBd_200",
        "Input Dimensions": "4x234x918",
        "Layers": "34",
        "FPS": "30.0",
        "Probability": "-",
        "Dependencies": "-"
    }
]

def generate_latex_table(data):
    header = r"""
\begin{tabular}{|l|c|c|c|c|l|}
\hline
\textbf{Name} & \textbf{Input Dimensions} & \textbf{Layers} & \textbf{FPS} & \textbf{Probability} & \textbf{Dependencies} \\
\hline
"""
    rows = ""
    for entry in data:
        rows += '\\text{' + entry['Name'].replace('_', '\\_') + '}' + f" & {entry['Input Dimensions']} & {entry['Layers']} & {entry['FPS']} & {entry['Probability']} & " + '\\text{' + entry['Dependencies'].replace('_', '\\_')  + '}' + f"\\\\\n\\hline\n"

    footer = r"\end{tabular}"
    return header + rows + footer

latex_code = generate_latex_table(data)
print(latex_code)
