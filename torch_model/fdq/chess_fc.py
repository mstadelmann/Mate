"""Plain fully-connected (dense) network with the same two
square-classification heads as chess_cnn.py - kept only for architecture
comparison, not as a serious engine.

Educational contrast with chess_cnn.py: the (16, 8, 8) board tensor is
flattened into a 1024-value vector before the first layer, so unlike the
CNN's 3x3 kernels - which see a local neighbourhood and reuse the same
small set of weights at every board position - every hidden unit here has
its own independent weight for each of the 1024 inputs. That means far
more parameters, no translation invariance, and no built-in notion of
which squares are adjacent; whatever spatial structure the CNN gets for
free, this network would have to infer from data alone (if at all).
"""

from typing import List, Optional, Tuple

import torch
from torch import nn


class ChessFC(nn.Module):
    def __init__(
        self,
        nb_in_channels: int = 16,
        board_size: int = 8,
        hidden_dims: Optional[List[int]] = None,
        dropout: float = 0.0,
    ) -> None:
        super().__init__()
        if hidden_dims is None:
            hidden_dims = [1024, 512]

        layers: List[nn.Module] = [nn.Flatten()]
        in_dim = nb_in_channels * board_size * board_size
        for out_dim in hidden_dims:
            layers.append(nn.Linear(in_dim, out_dim))
            layers.append(nn.ReLU(inplace=True))
            in_dim = out_dim

        self.backbone = nn.Sequential(*layers)
        # Regularization against overfitting the supervised training games.
        # Kept outside `backbone` so its layer indices - and so the
        # state_dict keys of existing checkpoints - stay unchanged; no
        # weights, and a no-op in eval() mode / the ONNX export.
        self.dropout = nn.Dropout(dropout)
        self.from_head = nn.Linear(in_dim, board_size * board_size)
        self.to_head = nn.Linear(in_dim, board_size * board_size)

    def forward(self, x: torch.Tensor) -> Tuple[torch.Tensor, torch.Tensor]:
        features = self.dropout(self.backbone(x))
        from_logits = self.from_head(features)
        to_logits = self.to_head(features)
        return from_logits, to_logits
