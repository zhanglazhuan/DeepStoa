# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DeepStoa is the official website for an e-ink phone-form-factor device targeting K-12 education. Users browse product info, place orders, and explore use cases.

The reference design to follow is [remarkable.com](https://remarkable.com).

## Tech Stack

- **Backend**: Python Django
- **Frontend**: React
- **Structure**: Monorepo (frontend and backend in one repository)

## Device Context

The product being marketed:
- 3.97" e-ink screen, 800×480, dual-point touch
- ESP32S3 processor, Zephyr OS
- Core features: reading, flashcard learning, time management
