pipeline {
    agent any
    stages {
        stage('Checkout') {
            steps { git branch: 'main', url: 'https://github.com/team/ai-agent.git' }
        }
        stage('Build') {
            steps {
                sh 'cmake -B build -S . -DCMAKE_BUILD_TYPE=Release'
                sh 'cmake --build build --config Release -j$(nproc)'
            }
        }
        stage('Test') {
            steps { sh 'cd build && ctest --output-on-failure -j$(nproc)' }
        }
        stage('Static Analysis') {
            steps { sh 'sonar-scanner' }
        }
        stage('Package') {
            steps { sh 'cd build && cpack -G TGZ' }
        }
        stage('Deploy') {
            steps { ansiblePlaybook playbook: 'ansible/deploy.yml', inventory: 'ansible/inventory/production' }
        }
    }
}
