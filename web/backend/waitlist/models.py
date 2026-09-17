from django.db import models

class WaitlistEntry(models.Model):
    name = models.CharField(max_length=200)
    email = models.EmailField(unique=True)
    role = models.CharField(max_length=20, choices=[
        ('student', 'Student'),
        ('teacher', 'Teacher'),
        ('parent', 'Parent'),
    ])
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"{self.name} <{self.email}>"
