import torch
import torch.nn as nn
import torch.optim as optim
import numpy as np
import pandas as pd
from sklearn.preprocessing import StandardScaler
# import pyautogui

class Autoencoder(nn.Module):
    def __init__(self, input_dim):
        super(Autoencoder, self).__init__()
        self.encoder = nn.Sequential(
            nn.Linear(input_dim, 16),
            nn.ReLU(),
            nn.Linear(16, 8),
            nn.ReLU()
        )
        self.decoder = nn.Sequential(
            nn.Linear(8, 16),
            nn.ReLU(),
            nn.Linear(16, input_dim)
        )
    
    def forward(self, x):
        x = self.encoder(x)
        x = self.decoder(x)
        return x

autoencoder=Autoencoder(2)
autoencoder.load_state_dict(torch.load(r'/home/UTTU/NewProj/OSProj/Input_logger/OS_zip_file/current_model.torch'))
autoencoder_ut=Autoencoder(2)
autoencoder_ut.load_state_dict(torch.load(r'/home/UTTU/NewProj/OSProj/Input_logger/OS_zip_file/current_model_ut.torch'))
reconstruction_threshold=pd.read_csv(r"/home/UTTU/NewProj/OSProj/Input_logger/OS_zip_file/reconstruction_threshold.csv").iloc[0,1]
current_data=np.array(pd.read_csv(r"/home/UTTU/NewProj/OSProj/Input_logger/OS_zip_file/Utkarsh.csv"))
scaler=StandardScaler()
scaled_data = scaler.fit_transform(current_data)
tensor_data=torch.Tensor(scaled_data)
reconstructed = autoencoder(tensor_data)

reconstruction_error = np.mean((scaled_data - reconstructed.detach().numpy())**2, axis=1)

print("When tested on Utkarsh dataset:")
pred_labels = []
for i,err in enumerate(reconstruction_error):
    if reconstruction_error[i] > reconstruction_threshold:
        pred_labels.append(0)
    else:
        pred_labels.append(1)

# if(sum(pred_labels)/len(pred_labels)>0.9):
#     pyautogui.alert("You're Valid!")
# else:
#     pyautogui.alert("You're Invalid!")

print("Samriddha-ness: ",sum(pred_labels)/len(pred_labels))

reconstructed = autoencoder_ut(tensor_data)
reconstruction_error = np.mean((scaled_data - reconstructed.detach().numpy())**2, axis=1)

reconstruction_threshold=pd.read_csv(r"/home/UTTU/NewProj/OSProj/Input_logger/OS_zip_file/reconstruction_threshold_ut.csv").iloc[0,1]

pred_labels = []
for i,err in enumerate(reconstruction_error):
    if reconstruction_error[i] > reconstruction_threshold:
        pred_labels.append(0)
    else:
        pred_labels.append(1)

# if(sum(pred_labels)/len(pred_labels)>0.9):
#     pyautogui.alert("You're Valid!")
# else:
#     pyautogui.alert("You're Invalid!")

print("Utkarsh-ness",sum(pred_labels)/len(pred_labels))

