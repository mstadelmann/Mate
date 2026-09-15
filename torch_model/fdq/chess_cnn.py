"""Small CNN backbone with two square-classification heads for chess move
prediction.

Kept independent of the supervised training loop in train.py on purpose: a
future self-play/RL training script could reuse this exact class and its
ONNX export contract (16-channel canonicalized input -> from_logits[64],
to_logits[64]) without any change here, only swapping out the training loop.
"""

from typing import List, Optional, Tuple

import torch
from torch import nn


class ChessCNN(nn.Module):
    def __init__(
        self,
        nb_in_channels: int = 16,
        conv_channels: Optional[List[int]] = None,
        kernel_size: int = 3,
    ) -> None:
        super().__init__()
        if conv_channels is None:
            conv_channels = [64, 64, 128, 128]

        padding = kernel_size // 2
        layers: List[nn.Module] = []
        in_ch = nb_in_channels
        for out_ch in conv_channels:
            layers.append(nn.Conv2d(in_ch, out_ch, kernel_size=kernel_size, padding=padding))
            layers.append(nn.BatchNorm2d(out_ch))
            layers.append(nn.ReLU(inplace=True))
            in_ch = out_ch

        self.backbone = nn.Sequential(*layers)
        self.from_head = nn.Conv2d(in_ch, 1, kernel_size=1)
        self.to_head = nn.Conv2d(in_ch, 1, kernel_size=1)

    def forward(self, x: torch.Tensor) -> Tuple[torch.Tensor, torch.Tensor]:
        features = self.backbone(x)
        from_logits = self.from_head(features).flatten(1)
        to_logits = self.to_head(features).flatten(1)
        return from_logits, to_logits
