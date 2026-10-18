# Publish this folder as a GitHub repository

This package is prepared as a local Git repository source tree; it has not been pushed to a remote GitHub account.

1. Create a new empty GitHub repository (for example, `esp-audio-dataset-recorder`). Do not initialize it with another README/license since these files already exist locally.
2. In a terminal, change into this folder:

   ```bash
   cd esp-audio-dataset-recorder
   git init
   git add .
   git commit -m "Initial ESP audio dataset recorder toolkit"
   git branch -M main
   git remote add origin https://github.com/YOUR_USERNAME/esp-audio-dataset-recorder.git
   git push -u origin main
   ```

3. Replace `YOUR_USERNAME` with your GitHub username.
4. Once pushed, review the README links, update the repository URL in `library.properties`, and enable GitHub Actions.
5. Before describing the repo as supporting another board/core version, compile and test that profile and update the support matrix.
