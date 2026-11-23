# CardioIA · App mobile (React Native + Expo)

Protótipo mobile do **Ir Além 2**: o usuário envia uma radiografia de tórax e vê a categoria detectada pela CNN (**Normal** ou **Cardiomegalia**), a probabilidade de cada categoria, a confiança e o mapa Grad-CAM.

| Tela | Conteúdo |
|---|---|
| **Envio** | Endereço da API e status da conexão · escolha da imagem (galeria, câmera ou exemplos do conjunto de teste) · escolha do modelo (ResNet50 TL ou CNN do zero) |
| **Resultado** | Categoria detectada em destaque · imagem com Grad-CAM (liga/desliga) · barras de probabilidade com o limiar · confiança, modelo e tempo · troca de modelo para comparar · aviso de uso responsável |

O app não roda o modelo no celular. Ele envia a imagem para o **backend Flask** (`src/api`), que faz o pré-processamento, a inferência e o Grad-CAM, e devolve um JSON.

## Como executar

Pré-requisitos: Node.js 18+, e no celular o app **Expo Go** (Android ou iOS) conectado à **mesma rede Wi-Fi** do computador.

1. Suba o backend na raiz do repositório:
   ```bash
   pip install -r src/api/requirements.txt
   python src/api/app.py          # escuta em 0.0.0.0:5000
   ```
2. Descubra o IP do computador na rede local (`ipconfig` no Windows, `ifconfig` / `ip a` no Linux/macOS).
3. Inicie o app:
   ```bash
   cd mobile
   npm install
   npx expo start                 # leia o QR code com o Expo Go
   ```
   Para pular a digitação do IP, use `EXPO_PUBLIC_API_URL=http://SEU_IP:5000 npx expo start`.
4. No app, confira o endereço em **Servidor**, toque em **Conectar** e faça a análise.

Sem celular, `npx expo start --web` abre a mesma interface no navegador (API em `http://localhost:5000`).

> Se o Windows bloquear a conexão do celular, libere a porta 5000 no Firewall para redes privadas.

## Estrutura

- `App.js`: as duas telas (Envio e Resultado) e a integração com a API (`fetch` + `FormData`).
- `app.json`: nome do app e textos de permissão da câmera e da galeria (plugin `expo-image-picker`).

Contrato da API usado pelo app: `GET /api/modelos`, `GET /api/amostras`, `POST /api/classificar` (`imagem` ou `amostra`, e `modelo`). Detalhes no README principal.
