"""Small CNN backbone with two square-classification heads for chess move
prediction.

Kept independent of the supervised training loop in train.py on purpose: a
future self-play/RL training script could reuse this exact class and its
ONNX export contract (16-channel canonicalized input -> from_logits[64],
to_logits[64]) without any change here, only swapping out the training loop.

With value_head=True the model also returns a third output, value[1] in
[-1, 1] - how good the position is for the side to move, trained on
Stockfish's evaluation (see data_preparation/label_with_stockfish.py). It
is appended last, so the ONNX contract's first two outputs stay unchanged
and src/chess_ML.cpp (which reads outputs 0 and 1) keeps working.
"""

from typing import List, Optional, Tuple, Union

import torch
from torch import nn


class ChessCNN(nn.Module):
    def __init__(
        self,
        nb_in_channels: int = 16,
        conv_channels: Optional[List[int]] = None,
        kernel_size: int = 3,
        dropout: float = 0.0,
        value_head: bool = False,
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
        # Regularization against overfitting the supervised training games.
        # No weights, so checkpoints load with or without it (e.g. the RL
        # warm start), and it's a no-op in eval() mode / the ONNX export.
        self.dropout = nn.Dropout(dropout)
        self.from_head = nn.Conv2d(in_ch, 1, kernel_size=1)
        self.to_head = nn.Conv2d(in_ch, 1, kernel_size=1)
        # AlphaZero-style value head: 1x1 conv down to one plane, then a
        # small MLP over the 64 squares to a single tanh output.
        self.value_head = (
            nn.Sequential(
                nn.Conv2d(in_ch, 1, kernel_size=1),
                nn.Flatten(),
                nn.Linear(64, 64),
                nn.ReLU(inplace=True),
                nn.Linear(64, 1),
                nn.Tanh(),
            )
            if value_head
            else None
        )

    def forward(
        self, x: torch.Tensor
    ) -> Union[Tuple[torch.Tensor, torch.Tensor], Tuple[torch.Tensor, torch.Tensor, torch.Tensor]]:
        features = self.dropout(self.backbone(x))
        from_logits = self.from_head(features).flatten(1)
        to_logits = self.to_head(features).flatten(1)
        if self.value_head is None:
            return from_logits, to_logits
        return from_logits, to_logits, self.value_head(features).flatten()
