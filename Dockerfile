# Use official light-weight Node image
FROM node:20-alpine

# Set working directory inside container
WORKDIR /app

# Copy package requirements first for layer caching optimization
COPY server/package*.json ./server/

# Install server package requirements
RUN cd server && npm install

# Copy all local project files including client assets and server scripts
COPY . .

# Expose the dashboard listening port
EXPOSE 3000

# CMD execution entrypoint to start backend
CMD ["node", "server/server.js"]
